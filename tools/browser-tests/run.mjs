// Test-only harness: packaged production wasm with real Chromium/SwiftShader.
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';
const root = resolve(process.argv[2] || '../../out/browser-qa/extracted');
const zip = resolve(process.argv[3] || '../../out/packages/drift-ocean-web-release.zip');
const output = resolve(process.argv[4] || '../../out/browser-qa/results');
await mkdir(output, {recursive:true});
const info = JSON.parse(await readFile(resolve(root, 'build-info.json')));
for (const [name, hash] of Object.entries(info.sha256 ?? Object.fromEntries(info.files.map(file => [file.path, file.sha256])))) {
  assert.equal(createHash('sha256').update(await readFile(resolve(root,name))).digest('hex'),hash,name);
}
const report = {kind:'Software GPU only; hardware and hosted acceptance unverified',
  rafDelayMs:100,
  zipSha256:createHash('sha256').update(await readFile(zip)).digest('hex'), cases:[], requests:[],
  launchArgs:['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-webgpu-adapter=swiftshader',
    '--use-angle=swiftshader','--use-vulkan=swiftshader','--disable-vulkan-surface','--enable-unsafe-swiftshader']};
const server = createServer(async (request,response) => {
  const pathname = new URL(request.url,'http://localhost').pathname;
  const file = resolve(root,'.'+(pathname.endsWith('/') ? pathname+'index.html' : pathname));
  if (!file.startsWith(root+sep)) {response.writeHead(403).end();return;}
  try {
    const bytes = await readFile(file);
    response.writeHead(200,{'Content-Type':file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'text/javascript':file.endsWith('.html')?'text/html':'text/plain','Cache-Control':'no-store'}).end(bytes);
    report.requests.push({path:pathname,status:200});
  } catch {response.writeHead(404).end();report.requests.push({path:pathname,status:404});}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser;
const observed = [];
const contexts = [];
const data = (page,name) => page.locator('#status').getAttribute('data-'+name);
async function until(read,predicate,label) {
  const deadline = Date.now()+30000;
  while(Date.now()<deadline) {const value=await read();if(predicate(value))return value;await delay(50);}
  throw Error('Timed out: '+label);
}
const playing = page=>until(async()=>{
  const state=await data(page,'state');
  if(state==='failed'||state==='device-lost')throw Error('Scene '+state+': '+JSON.stringify(await page.locator('#status').evaluate(el=>({...el.dataset}))));
  return state;
},v=>v==='playing','playing');
async function shot(page) {
  // A bottom crop excludes the status and controls and captures the compositor.
  const box=await page.locator('canvas').boundingBox();
  return page.screenshot({clip:{x:box.x,y:box.y+box.height*0.65,width:Math.min(box.width,540),height:Math.floor(box.height*0.35)}});
}
function variation(bytes) {
  const {data}=PNG.sync.read(bytes);let sum=0,square=0,count=0;
  for(let p=0;p<data.length;p+=4){const v=data[p]+data[p+1]+data[p+2];sum+=v;square+=v*v;++count;}
  return Math.sqrt(square/count-(sum/count)**2);
}
function difference(a,b) {
  const first=PNG.sync.read(a),second=PNG.sync.read(b);assert.equal(first.data.length,second.data.length);
  let sum=0;for(let p=0;p<first.data.length;++p)sum+=Math.abs(first.data[p]-second.data[p]);
  return sum/first.data.length;
}
async function context(fault='none',options={}) {
  const c=await browser.newContext({viewport:{width:960,height:540},...options});
  contexts.push(c);
  c.on('page',page=>{const messages=[];observed.push({page,messages});page.on('pageerror',e=>messages.push(String(e)));page.on('console',m=>{messages.push({type:m.type(),text:m.text()});});});
  await c.addInitScript(fault=>{
    // Bound the expensive procedural shader queue on SwiftShader. This is a
    // test-only 100 ms RAF delay, not evidence for interactive frame rates.
    const raf=window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame=cb=>raf(time=>setTimeout(()=>cb(time),100));
    const get=HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext=function(type,...args){
      if(type==='webgpu'&&fault==='surface')return null;
      if(type==='webgl2'&&fault==='both')return null;
      return get.call(this,type,...args);
    };
    if(fault==='missing'||fault==='both') {Object.defineProperty(navigator,'gpu',{value:undefined});return;}
    const gpu=navigator.gpu;if(!gpu)return;
    const request=gpu.requestAdapter.bind(gpu);
    gpu.requestAdapter=async options=>{
      if(fault==='adapter')return null;
      const a=await request(options);if(!a)return a;
      const device=a.requestDevice.bind(a);
      a.requestDevice=async options=>{
        if(fault==='device')throw Error('QA rejected device');
        const d=await device(options);window.__qaDevice=d;window.__qaErrors=[];
        d.addEventListener('uncapturederror',e=>window.__qaErrors.push(e.error.message));return d;
      };
      return a;
    };
  },fault);
  return c;
}
async function run(name,task) {
  if(process.env.DRIFT_TEST_FILTER&&!name.includes(process.env.DRIFT_TEST_FILTER))return;
  const entry={name,status:'running'};report.cases.push(entry);
  try {await task(entry);entry.status='passed';} catch(error){
    entry.status='failed';entry.error=String(error);entry.diagnostics=[];
    for(const {page,messages} of observed){if(page.isClosed())continue;
      const item={url:page.url(),messages};entry.diagnostics.push(item);
      try{item.status=await page.locator('#status').textContent();item.attributes=await page.locator('#status').evaluate(el=>({...el.dataset}));item.gpuErrors=await page.evaluate(()=>window.__qaErrors);await page.screenshot({path:resolve(output,'failure-'+report.cases.length+'.png')});}catch{}
    }
  } finally {for(const c of contexts.splice(0))await c.close();}
  console.log(JSON.stringify(entry));
  await writeFile(resolve(output,'progress.json'),JSON.stringify(report,null,2)+'\n');
}
async function frozen(page) {
  await page.evaluate(()=>{Module._OceanSetPaused(1);Module._OceanResetSimulation();});
  const before=Number(await data(page,'frames'));
  await until(()=>data(page,'frames'),v=>Number(v)>before+2,'frozen frame presented');
  await delay(150);
  return shot(page);
}
try {
  browser=await chromium.launch({headless:true,args:report.launchArgs});
  report.browserVersion=browser.version();assert.equal(report.browserVersion,'140.0.7339.186');
  const reference={};
  for(const backend of ['webgpu','webgl2']) await run(backend+': real ocean pixels, controls, pause, restart',async entry=>{
    const c=await context();const page=await c.newPage();await page.goto(base+'/index.html?backend='+backend);await playing(page);
    await page.locator('#mode').click();
    await until(()=>page.locator('#game-status').getAttribute('data-enabled'),v=>v==='false','tuning mode');
    assert.equal(await data(page,'backend'),backend);
    reference[backend]=await frozen(page);entry.pixelVariation=variation(reference[backend]);
    assert(entry.pixelVariation>8,'Ocean is a flat clear color');
    await page.screenshot({path:resolve(output,backend+'.png')});
    const paused=await shot(page);await delay(250);assert(difference(paused,await shot(page))<0.1,'Paused ocean moved');
    await page.locator('#panel summary').click();
    await page.locator('#pause').click(); // Clock was paused through the bridge; this resumes it.
    await delay(350);assert(difference(paused,await shot(page))>0.2,'Resumed ocean stayed frozen');
    await page.locator('[data-preset="2"]').click();
    await page.locator('#export').click();const settings=JSON.parse(await page.locator('#json').inputValue());
    assert(settings.waveIntensity>0.55,'Choppy preset did not reach the settings store');
    await page.locator('#json').fill('{invalid');await page.locator('#import').click();
    assert.equal(await page.locator('#io-message').getAttribute('data-kind'),'error');
    await page.locator('#reset-all').click();await page.locator('#export').click();
    assert.equal(JSON.parse(await page.locator('#json').inputValue()).waveIntensity,0.55);
    // Restart replaces the canvas: focus and input ownership must survive it.
    await page.locator('#restart').click();await playing(page);
    await page.locator('canvas').click({position:{x:100,y:450}});await page.keyboard.press('d');
    assert(await page.locator('canvas').evaluate(c=>document.activeElement===c),'Canvas lost focus binding after replacement');
    await page.locator('#speed').focus();await page.keyboard.press('ArrowRight');
    assert.equal(await page.locator('#speed').evaluate(el=>document.activeElement===el),true);
    if(backend==='webgpu')assert.deepEqual(await page.evaluate(()=>window.__qaErrors),[]);
    assert.equal(observed.find(o=>o.page===page).messages.filter(m=>typeof m==='string'||m.type==='error').length,0);
    await c.close();
  });
  await run('WebGPU and WebGL 2 ocean orientation and palette agree',async entry=>{
    entry.meanPixelDifference=difference(reference.webgpu,reference.webgl2);
    assert(entry.meanPixelDifference<3,'Backends disagree on ocean coordinates or palette');
  });
  for(const fault of ['missing','adapter','device','surface']) await run('Auto fallback after '+fault,async entry=>{
    const c=await context(fault);const page=await c.newPage();await page.goto(base+'/');await playing(page);
    assert.equal(await data(page,'backend'),'webgl2');assert(Number(await data(page,'webgpu-error'))>0);
    assert(variation(await frozen(page))>8);await page.screenshot({path:resolve(output,'fallback-'+fault+'.png')});
    entry.backend=await data(page,'backend');assert.equal(observed.find(o=>o.page===page).messages.filter(m=>typeof m==='string'||m.type==='error').length,0);await c.close();
  });
  await run('forced WebGPU fails explicitly without fallback',async()=>{
    const c=await context('missing');const page=await c.newPage();await page.goto(base+'/?backend=webgpu');
    await until(()=>data(page,'state'),s=>s==='failed','forced failure');assert.equal(await data(page,'webgl-error'),'0');await c.close();
  });
  await run('both unavailable: readable narrow error and restart',async()=>{
    const c=await context('both',{viewport:{width:300,height:240}});const page=await c.newPage();await page.goto(base+'/');
    await until(()=>data(page,'state'),s=>s==='failed','failed');assert.equal(await page.locator('#panel').isVisible(),false);
    assert.equal(await page.locator('#status-restart').isVisible(),true);assert.equal(await data(page,'frames'),'0');
    const box=await page.locator('#status-region').boundingBox();assert(box.x>=0&&box.x+box.width<=300&&box.y+box.height<=240);
    await page.screenshot({path:resolve(output,'both-unavailable.png')});await page.locator('#status-restart').click();
    await until(()=>data(page,'state'),s=>s==='failed','failed restart');await c.close();
  });
  await run('narrow responsive controls, DPR, iframe',async()=>{
    const c=await context('missing',{viewport:{width:320,height:360},deviceScaleFactor:1.5});const page=await c.newPage();await page.goto(base+'/');await playing(page);
    await page.locator('#mode').click();
    await until(()=>page.locator('#game-status').getAttribute('data-enabled'),v=>v==='false','narrow tuning mode');
    await page.locator('#panel summary').click();
    const status=await page.locator('#status-region').boundingBox(),panel=await page.locator('#panel').boundingBox();
    assert(status.y+status.height<=panel.y&&panel.x+panel.width<=320&&panel.y+panel.height<=360,'Responsive panel overlaps status or escapes viewport');
    const dims=await page.locator('canvas').evaluate(c=>({w:c.width,h:c.height,cw:c.clientWidth,ch:c.clientHeight}));
    assert(Math.abs(dims.w-dims.cw*1.5)<=1&&Math.abs(dims.h-dims.ch*1.5)<=1);
    await page.screenshot({path:resolve(output,'narrow-controls.png')});
    await page.goto(base+'/iframe.html');const frame=await until(()=>Promise.resolve(page.frames().find(f=>f.url().endsWith('/index.html'))),Boolean,'iframe');
    await playing(frame);assert.equal(await data(frame,'backend'),'webgl2');await c.close();
  });
  await run('WebGL 2 loss, restart and repeated resize',async entry=>{
    const c=await context('missing');const page=await c.newPage();await page.goto(base+'/');await playing(page);
    entry.loseContext=await page.evaluate(()=>{const ext=document.querySelector('canvas').getContext('webgl2').getExtension('WEBGL_lose_context');if(ext)ext.loseContext();return !!ext;});
    assert(entry.loseContext,'Required test extension unavailable');await until(()=>data(page,'state'),s=>s==='device-lost','context loss');
    await page.screenshot({path:resolve(output,'context-lost.png')});await page.locator('#status-restart').click();await playing(page);
    for(let i=0;i<3;++i){await page.setViewportSize({width:720+i*50,height:480});await page.evaluate(()=>Module._OceanRestart());await playing(page);}
    assert(variation(await frozen(page))>8);await c.close();
  });
  for(const file of ['index.js','index.wasm']) await run('network failure '+file,async()=>{
    const c=await context();const page=await c.newPage();await page.route('**/'+file,r=>r.abort());await page.goto(base+'/');
    await until(()=>data(page,'state'),s=>s==='failed','network error');assert.equal(await page.locator('#panel').isVisible(),false);
    assert.equal(await page.locator('#status-restart').isVisible(),true);await c.close();
  });
  assert.equal(report.cases.filter(c=>c.status==='failed').length,0,'Browser acceptance failed');
} catch(error) {
  report.failure=String(error);process.exitCode=1;
  report.diagnostics=[];
  for(const {page,messages} of observed){if(page.isClosed())continue;
    const item={url:page.url(),messages};report.diagnostics.push(item);
    try {item.status=await page.locator('#status').textContent();item.attributes=await page.locator('#status').evaluate(el=>({...el.dataset}));item.gpuErrors=await page.evaluate(()=>window.__qaErrors);await page.screenshot({path:resolve(output,'failure-'+report.diagnostics.length+'.png')});}catch{}
  }
} finally {
  await browser?.close();server.close();await writeFile(resolve(output,'report.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify(report,null,2));
}
