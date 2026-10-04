// Real pointer strokes and presented water on both renderers. Software-GPU
// flags and emulated touch do not establish physical-device performance.
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';
import {canvasView} from './view.mjs';
import {PNG} from 'pngjs';

const root = resolve(process.argv[2] || '../../out/browser-qa/extracted');
const output = resolve(process.argv[3] || '../../out/ocean-feedback/browser');
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
const report = {kind: 'Software GPU; mouse and emulated touch', cases: [],
  rafDelayMs: {mouse: 500, touch: 500},
  touchDpr: Number(process.env.DRIFT_GESTURE_DPR || 1),
  renderScale: Number(process.env.DRIFT_GESTURE_SCALE || 1),
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-webgpu-adapter=swiftshader',
    '--use-angle=swiftshader', '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader']};
const state = page => page.locator('#game-status').evaluate(el => ({...el.dataset}));
async function until(read, predicate, name) {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const value = await read();
    if (predicate(value)) return value;
    await delay(50);
  }
  throw Error('Timeout: ' + name);
}
async function pauseWithRenderedFrames(page) {
  const before = await page.evaluate(() => {
    Module._OceanSetPaused(1);
    return Number(document.getElementById('status').dataset.frames);
  });
  await until(() => state(page), s => s.paused === 'true', 'pause telemetry');
  // Telemetry confirms a submitted frame, not its presentation. In particular,
  // a 150 ms sleep is shorter than our 500 ms RAF cadence. Render subsequent
  // frozen frames before comparing screenshots, as in the renderer suite.
  await until(() => page.locator('#status').getAttribute('data-frames'),
    n => Number(n) >= before + 3, 'paused frames rendered');
}
let browser, activePage;
try {
  browser = await chromium.launch({headless: true, args: report.args});
  report.browserVersion = browser.version();
  for (const backend of ['webgpu', 'webgl2']) {
    for (const mobile of [false, true]) {
      const name = backend + (mobile ? '-touch' : '-mouse');
      if (process.env.DRIFT_GESTURE_FILTER && !name.includes(process.env.DRIFT_GESTURE_FILTER)) continue;
      const viewport = mobile ? {width: 390, height: 844} : {width: 960, height: 600};
      const context = await browser.newContext({viewport, hasTouch: mobile, deviceScaleFactor: process.env.DRIFT_GESTURE_SCALE ? report.renderScale : mobile ? report.touchDpr : 1});
      context.setDefaultTimeout(60000);
      await context.addInitScript(rafDelay => {
        const raf = requestAnimationFrame;
        window.requestAnimationFrame = callback => raf(time => setTimeout(() => callback(time), rafDelay));
      }, 500);
      const page = await context.newPage(); activePage = page;
      const errors = [];
      page.on('console', message => { if (message.type() === 'warning' || message.type() === 'error') errors.push(message.text()); });
      page.on('pageerror', error => errors.push(String(error)));
      await page.goto(base + '/?backend=' + backend);
      await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'startup');
      await until(() => page.locator('#status').getAttribute('data-frames'), n => Number(n) > 2, 'pixels');
      const view = await canvasView(page);
      const screen = view.point;
      // Exclude the 3 CSS-pixel focus rim, including on an inset mobile canvas.
      const waterClip = {x: view.box.x + 4, y: view.box.y + 4,
        width: view.box.width - 8, height: view.box.height - 8};
      const cdp = mobile ? await context.newCDPSession(page) : null;
      const pointer = async (phase, point) => {
        if (mobile) {
          await cdp.send('Input.dispatchTouchEvent', {type: phase === 0 ? 'touchStart' : phase === 1 ? 'touchMove' : 'touchEnd',
            touchPoints: phase === 2 ? [] : [{...point, id: 1}]});
        } else {
          if (phase !== 2) await page.mouse.move(point.x, point.y);
          if (phase === 0) await page.mouse.down();
          if (phase === 2) await page.mouse.up();
        }
      };
      const reset = async () => {
        await page.evaluate(() => { Module._OceanResetSimulation(); Module._OceanSetPaused(0); });
        await until(() => state(page), s => Number(s.energy) === 0 && Number(s.placements) === 0, 'reset');
      };
      await pauseWithRenderedFrames(page);
      await page.screenshot({path: resolve(output, name + '-ocean.png')});
      await page.evaluate(() => { Module._OceanSetWaveIntensity(0); Module._OceanSetPaused(0); });
      await delay(700);
      await reset();
      const start = screen(-14, -60), end = screen(-4, -60);
      await pointer(0, start);
      for (let i = 1; i <= 16; ++i) {
        await pointer(1, {x: start.x + (end.x - start.x) * i / 16, y: start.y});
      }
      await pointer(2, end);
      await until(() => state(page), s => Number(s.energy) > 10 && Number(s.height) > 0.01, 'momentum produces a height wave');
      assert.equal(Number((await state(page)).placements), 0, 'Swipe also emitted a tap ripple');
      await until(() => state(page), s => Number(s.x) > 0.1, 'wave transports boat in swipe direction');
      const wave = await state(page);
      await page.screenshot({path: resolve(output, name + '-wave.png')});
      assert(Number(wave.y) > -60, 'Background river flow is missing');
      await reset();
      // World counterclockwise appears counterclockwise on the rendered ocean.
      for (const sign of [1, -1]) {
        const initial = screen(0, -60);
        await pointer(0, initial);
        for (let i = 1; i <= 48; ++i) {
          const angle = sign * i * Math.PI * 2 / 48;
          await pointer(1, screen(-6 + Math.cos(angle) * 6, -60 + Math.sin(angle) * 6));
        }
        await pointer(2, initial);
        await until(() => state(page), s => Number(s.curl) > 0.2 && Number(s.energy) > 10, 'stirring produces vorticity');
        const circulation = await page.evaluate(() => {
          let sum = 0;
          for (let i = 0; i < 64; ++i) {
            const angle = (i + 0.5) * Math.PI * 2 / 64;
            const x = -6 + Math.cos(angle) * 6, y = -60 + Math.sin(angle) * 6;
            sum += (-Module._DriftWaterSample(x, y, 0) * Math.sin(angle) +
                     Module._DriftWaterSample(x, y, 1) * Math.cos(angle)) * 6 * Math.PI * 2 / 64;
          }
          return sum;
        });
        assert(circulation * sign > 10, 'Circulation disagrees with the drawn direction');
        await until(() => page.evaluate(() => Module._DriftWaterSample(0, -60, 1)), v => v * sign > 0.08, 'vortex water reaches boat');
        assert.equal(Number((await state(page)).placements), 0, 'Circle emitted a tap ripple');
        const firstMaterial = await page.evaluate(() => Module._DriftWaterSample(0, -60, 5));
        const moving = await state(page);
        await until(() => state(page), s => Number(s.ticks) >= Number(moving.ticks) + 24, 'released field evolves');
        const laterMaterial = await page.evaluate(() => Module._DriftWaterSample(0, -60, 5));
        assert((laterMaterial - firstMaterial) * sign > 0.05, 'Water pattern does not advect after release');
        await pauseWithRenderedFrames(page);
        const frozen = await state(page);
        const firstPixels = PNG.sync.read(await page.screenshot({clip: waterClip,
          path: resolve(output, name + (sign > 0 ? '-ccw.png' : '-cw.png'))}));
        const capturedFrame = Number(await page.locator('#status').getAttribute('data-frames'));
        await until(() => page.locator('#status').getAttribute('data-frames'),
          n => Number(n) >= capturedFrame + 2, 'subsequent paused frames rendered');
        assert.equal((await state(page)).ticks, frozen.ticks, 'Pause advances current');
        const laterPixels = PNG.sync.read(await page.screenshot({clip: waterClip, path: resolve(output,
          name + (sign > 0 ? '-ccw-paused.png' : '-cw-paused.png'))}));
        assert(firstPixels.data.equals(laterPixels.data), 'Paused water pixels keep moving');
        if (sign > 0) {
          await page.evaluate(() => Module._OceanRestart());
          await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'restart with vortex');
          await until(() => page.locator('#status').getAttribute('data-frames'), n => Number(n) > 2, 'restart pixels');
          assert.equal((await state(page)).energy, frozen.energy, 'Restart loses field state');
          assert.equal((await state(page)).ticks, frozen.ticks, 'Restart advances paused current');
        }
        await reset();
      }
      // Pointer identity and cancellation: a second finger cannot end a stroke.
      await page.evaluate(() => {
        const canvas = document.getElementById('canvas');
        const rect = canvas.getBoundingClientRect();
        const event = (type, id, primary = true) => new PointerEvent(type, {bubbles: true,
          pointerId: id, isPrimary: primary, button: 0, clientX: rect.left + rect.width / 2, clientY: rect.top + rect.height / 2 + 60 * rect.height / Math.max(90, 70 / (rect.width / rect.height))});
        canvas.dispatchEvent(event('pointerdown', 71));
        canvas.dispatchEvent(event('pointerup', 72, false));
        canvas.dispatchEvent(event('pointercancel', 71));
        canvas.dispatchEvent(event('pointerup', 71));
      });
      await delay(250);
      assert.equal(Number((await state(page)).placements), 0, 'Cancelled pointer emitted a tap');
      assert.equal(Number((await state(page)).energy), 0, 'Cancelled pointer injected momentum');
      assert.equal(errors.length, 0, errors.join('\n'));
      report.cases.push({name, status: 'passed', wave, errors});
      console.log(name + ': passed');
      await context.close(); activePage = undefined;
    }
  }
} catch (error) {
  report.failure = String(error); process.exitCode = 1;
  if (activePage) {
    report.game = await state(activePage).catch(() => null);
    await activePage.screenshot({path: resolve(output, 'failure.png')}).catch(() => {});
  }
} finally {
  await browser?.close(); server.close();
  await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
}
