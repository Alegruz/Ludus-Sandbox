// Real rendered game/input checks. SwiftShader and emulated touch are not
// physical GPU/mobile acceptance or frame-rate measurements.
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';

const root = resolve(process.argv[2] || '../../out/build/web-emscripten-development');
const output = resolve(process.argv[3] || '../../out/ripple-qa');
await mkdir(output, {recursive: true});
const server = createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const file = resolve(root, '.' + (pathname.endsWith('/') ? pathname + 'index.html' : pathname));
  if (!file.startsWith(root + sep)) { response.writeHead(403).end(); return; }
  try {
    const bytes = await readFile(file);
    response.writeHead(200, {'Content-Type': file.endsWith('.wasm') ? 'application/wasm' :
      file.endsWith('.js') ? 'text/javascript' : 'text/html', 'Cache-Control': 'no-store'}).end(bytes);
  } catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const report = {kind: 'Real software GPU, emulated touch; physical devices unverified', cases: [],
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-webgpu-adapter=swiftshader',
    '--use-angle=swiftshader', '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader']};
let browser;
let activePage;
async function until(read, predicate, name) {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) { const value = await read(); if (predicate(value)) return value; await delay(50); }
  throw Error('Timeout: ' + name);
}
const state = page => page.locator('#game-status').evaluate(el => ({...el.dataset}));
const waitFrames = (page, count) => until(() => page.locator('#status').getAttribute('data-frames'),
  frames => Number(frames) >= count, 'frames');

try {
  browser = await chromium.launch({headless: true, args: report.args});
  report.browserVersion = browser.version();
  for (const backend of ['webgpu', 'webgl2']) {
    for (const mobile of [false, true]) {
      const viewport = mobile ? {width: 390, height: 844} : {width: 960, height: 540};
      const context = await browser.newContext({viewport, hasTouch: mobile, deviceScaleFactor: mobile ? 2 : 1});
      // Bound expensive software-GPU work. No interactive timing claim is made.
      await context.addInitScript(() => {
        const raf = requestAnimationFrame;
        window.requestAnimationFrame = callback => raf(time => setTimeout(() => callback(time), 50));
      });
      const page = await context.newPage(); activePage = page;
      const errors = [];
      page.on('console', message => { if(message.type()==='warning'||message.type()==='error') errors.push(message.text()); });
      await page.addInitScript(() => { window.__inputTrace=[]; for (const type of ['pointerdown','pointercancel','focus','blur']) window.addEventListener(type, e => window.__inputTrace.push({type, target:e.target.id, primary:e.isPrimary, button:e.button}), true); });
      page.on('pageerror', error => errors.push(String(error)));
      await page.goto(base + '/?backend=' + backend);
      await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'startup');
      await waitFrames(page, 3);
      assert.equal(await page.locator('#status').getAttribute('data-backend'), backend);
      const initial = await state(page);
      assert.equal(Number(initial.placements), 0);
      const viewHeight = Math.max(90, 70 / (viewport.width / viewport.height));
      const x = viewport.width / 2 - 5 * viewport.height / viewHeight;
      const y = viewport.height / 2;
      if (mobile) await page.touchscreen.tap(x, y); else await page.mouse.click(x, y);
      await until(() => state(page), s => Number(s.contacts) === 1 && Number(s.x) > 0.1, 'ring push');
      await page.locator('#game-pause').click();
      await until(() => state(page), s => s.paused === 'true', 'pause');
      const frozen = await state(page);
      assert.equal(Number(frozen.placements), 1, 'Tap/click emitted duplicate rings');
      assert.equal(Number(frozen.contacts), 1);
      assert(Math.abs(Number(frozen.y)) < 0.001, 'Push direction/mapping is incorrect');
      await delay(300);
      const later = await state(page);
      assert.equal(later.ticks, frozen.ticks, 'Paused game advanced');
      assert.equal(later.x, frozen.x, 'Paused boat moved');
      const name = backend + (mobile ? '-touch' : '-mouse');
      await page.screenshot({path: resolve(output, name + '.png')});
      await page.mouse.click(x, y); await delay(150);
      assert.equal((await state(page)).placements, frozen.placements, 'Paused input created a ripple');

      await page.locator('#game-reset').click(); await delay(150);
      const reset = await state(page);
      assert.equal(Number(reset.x), 0); assert.equal(Number(reset.placements), 0); assert.equal(Number(reset.rings), 0);
      const before = Number(await page.locator('#status').getAttribute('data-frames'));
      await waitFrames(page, before + 2);
      const screenshot = PNG.sync.read(await page.screenshot());
      const cx = Math.floor(screenshot.width / 2), cy = Math.floor(screenshot.height / 2);
      const pixel = (cy * screenshot.width + cx) * 4;
      assert(screenshot.data[pixel] > screenshot.data[pixel + 2] + 15, 'Boat is not visible at the physical center');

      await page.setViewportSize({width: 844, height: 390});
      await delay(200);
      assert.equal(Number((await state(page)).x), 0, 'Resize moved physics');
      await page.locator('#mode').click(); await delay(150);
      assert.equal((await state(page)).enabled, 'false');
      await page.locator('#mode').click(); await delay(150);
      assert.equal((await state(page)).enabled, 'true');
      await page.evaluate(() => { Module._OceanSetPaused(0); Module._DriftSetFocused(0); });
      const blurred = await state(page); await delay(300);
      assert.equal((await state(page)).ticks, blurred.ticks, 'Blurred game advanced');
      await page.evaluate(() => Module._DriftSetFocused(1));
      await until(() => state(page), s => Number(s.ticks) > Number(blurred.ticks), 'focus resume');
      // The engine replaces the canvas during startup/restart. Input must bind
      // to the replacement and restart must preserve the paused world.
      await page.locator('#game-pause').click();
      await until(() => state(page), s => s.paused === 'true', 'pause before restart');
      const preserved = await state(page);
      await page.evaluate(() => Module._OceanRestart());
      await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'restart');
      await waitFrames(page, 3);
      assert.equal((await state(page)).ticks, preserved.ticks, 'Restart advanced paused physics');
      assert.equal((await state(page)).x, preserved.x, 'Restart reset boat');
      assert.equal(Number((await state(page)).placements), 0, 'UI emitted a ripple');
      await page.locator('#game-pause').click();
      await until(() => state(page), s => s.paused === 'false', 'resume after restart');
      // A cancellation before the next RAF must discard the queued placement.
      await page.evaluate(() => {
        const canvas = document.getElementById('canvas');
        const rect = canvas.getBoundingClientRect();
        canvas.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true, isPrimary: true,
          button: 0, clientX: rect.left + rect.width / 2, clientY: rect.top + rect.height / 2}));
        canvas.dispatchEvent(new PointerEvent('pointercancel', {bubbles: true, isPrimary: true}));
      });
      await delay(250);
      assert.equal(Number((await state(page)).placements), 0, 'Cancelled pointer emitted a ripple');
      const rx = 844 / 2 + 5 * 390 / 90, ry = 390 / 2;
      if (mobile) await page.touchscreen.tap(rx, ry); else await page.mouse.click(rx, ry);
      await until(() => state(page), s => Number(s.contacts) === 1 && Number(s.x) < -0.1, 'restart input');
      assert.equal(Number((await state(page)).placements), 1);
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({name, status: 'passed', push: frozen, errors});
      await context.close(); activePage = undefined;
    }
  }
} catch (error) {
  report.failure = String(error); process.exitCode = 1;
  if (activePage) {
    report.game = await state(activePage).catch(() => null);
    report.input = await activePage.evaluate(() => ({trace: window.__inputTrace, placeType: typeof Module._DriftPlace})).catch(() => null);
    report.scene = await activePage.locator('#status').textContent().catch(() => null);
    await activePage.screenshot({path: resolve(output, 'failure.png')}).catch(() => {});
  }
} finally {
  await browser?.close(); server.close();
  await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
}
