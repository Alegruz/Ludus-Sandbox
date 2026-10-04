// Finite splashes, propagated surface height and ambient sea in the real ZIP.
// SwiftShader and emulated touch establish behavior, not hardware performance.
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {createHash} from 'node:crypto';
import {chromium} from 'playwright';

const root = resolve(process.argv[2]);
const output = resolve(process.argv[3]);
await mkdir(output, {recursive: true});
const info = JSON.parse(await readFile(resolve(root, 'build-info.json')));
for (const [name, hash] of Object.entries(info.sha256))
  assert.equal(createHash('sha256').update(await readFile(resolve(root, name))).digest('hex'), hash);
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = resolve(root, '.' + (pathname.endsWith('/') ? pathname + 'index.html' : pathname));
  if (!file.startsWith(root + sep)) { response.writeHead(403).end(); return; }
  try {
    const bytes = await readFile(file);
    response.writeHead(200, {'Content-Type': file.endsWith('.wasm') ? 'application/wasm' :
      file.endsWith('.js') ? 'text/javascript' : 'text/html'}).end(bytes);
  } catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const report = {kind: 'Software GPU, mouse and emulated touch', engine: info.engine_revision,
  payload: info.sha256, cases: []};
const state = page => page.locator('#game-status').evaluate(el => ({...el.dataset}));
async function until(read, test, label) {
  const deadline = Date.now() + 45000;
  while (Date.now() < deadline) { const value = await read(); if (test(value)) return value; await delay(50); }
  throw Error('Timeout: ' + label);
}
let browser;
try {
  browser = await chromium.launch({headless: true, args: ['--enable-unsafe-webgpu',
    '--enable-features=Vulkan', '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader',
    '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader']});
  report.browserVersion = browser.version();
  for (const backend of ['webgpu', 'webgl2']) {
    const mobile = backend === 'webgl2';
    const viewport = mobile ? {width: 390, height: 844} : {width: 720, height: 600};
    const context = await browser.newContext({viewport, hasTouch: mobile});
    await context.addInitScript(() => {
      const raf = requestAnimationFrame;
      window.requestAnimationFrame = callback => raf(time => setTimeout(() => callback(time), 100));
    });
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => { if (['warning', 'error'].includes(message.type())) errors.push(message.text()); });
    await page.goto(`http://127.0.0.1:${server.address().port}/?backend=${backend}`);
    await until(() => page.locator('#status').getAttribute('data-frames'), n => Number(n) > 3, 'startup');
    assert.match(await page.locator('#game-instructions').textContent(), /Drag.*current.*Tap for splashes/);
    const sea = await state(page);
    assert(Number(sea.energy) > 1 && Number(sea.height) > 0.05, 'Ambient sea has no waves');
    await page.evaluate(() => Module._OceanSetPaused(1));
    await delay(200);
    await page.screenshot({path: resolve(output, backend + '-ambient.png')});
    await page.evaluate(() => { Module._OceanSetWaveIntensity(0); Module._OceanSetPaused(0); });
    await delay(300);
    await page.locator('#game-reset').click();
    await until(() => state(page), s => Number(s.energy) === 0, 'calm reset');
    const height = Math.max(90, 70 / (viewport.width / viewport.height));
    const tap = async x => {
      const px = viewport.width / 2 + x * viewport.height / height;
      const py = viewport.height / 2 + 22 * viewport.height / height;
      if (mobile) await page.touchscreen.tap(px, py); else await page.mouse.click(px, py);
    };
    await tap(-8);
    await until(() => state(page), s => Number(s.placements) === 1 && Number(s.cooldown) === 0, 'first finite splash');
    await tap(8);
    const placed = await until(() => state(page), s => Number(s.placements) === 2, 'second finite splash');
    await until(() => state(page), s => Number(s.ticks) > Number(placed.ticks) + 75, 'wave overlap');
    await page.evaluate(() => Module._OceanSetPaused(1));
    await delay(200);
    const waves = await state(page);
    const between = await page.evaluate(() => Module._DriftWaterSample(0, -22, 2));
    assert(Math.abs(between) > 0.01, 'Waves have not propagated outside the splash footprints');
    assert(Number(waves.energy) > 1, 'Disturbance expired with its source');
    await page.screenshot({path: resolve(output, backend + '-overlap.png')});
    assert.equal(errors.length, 0, errors.join('\n'));
    report.cases.push({backend, input: mobile ? 'touch' : 'mouse', status: 'passed', sea, waves, between, errors});
    console.log(backend + ': ambient sea and overlapping finite splashes passed');
    await context.close();
  }
} catch (error) { report.failure = String(error); process.exitCode = 1; }
finally {
  await browser?.close(); server.close();
  await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
}
