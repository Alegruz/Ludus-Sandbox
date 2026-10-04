// Real rendered game/input checks. SwiftShader and emulated touch are not
// physical GPU/mobile acceptance or frame-rate measurements.
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {chromium} from 'playwright';
import {canvasView} from './view.mjs';
import {selectBrowserCases} from './cases.mjs';
import {PNG} from 'pngjs';

const selectedCases = selectBrowserCases(process.env.DRIFT_GAME_FILTER);
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
  rafDelayMs: {mouse: Number(process.env.DRIFT_QA_RAF_MS || 40), touch: Number(process.env.DRIFT_QA_RAF_MS || 40)},
  touchDpr: Number(process.env.DRIFT_GAME_DPR || 1),
  renderScale: Number(process.env.DRIFT_GAME_SCALE || 1),
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
      const name = backend + (mobile ? '-touch' : '-mouse');
      if (!selectedCases.includes(name)) continue;
      const viewport = mobile ? {width: 390, height: 844} : {width: 960, height: 540};
      const context = await browser.newContext({viewport, hasTouch: mobile, deviceScaleFactor: process.env.DRIFT_GAME_SCALE ? report.renderScale : mobile ? report.touchDpr : 1});
      // Bound expensive software-GPU work. No interactive timing claim is made.
      context.setDefaultTimeout(60000);
      await context.addInitScript(rafDelay => {
        const raf = requestAnimationFrame;
        window.requestAnimationFrame = callback => raf(time => setTimeout(() => callback(time), rafDelay));
      }, report.rafDelayMs[mobile ? 'touch' : 'mouse']);
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
      await page.evaluate(() => Module._OceanSetWaveIntensity(0));
      await delay(200);
      await page.locator('#game-reset').click();
      assert(await page.locator('#panel').isHidden(), 'Tuning controls obscure the game');
      const view = await canvasView(page);
      const {x, y} = view.point(-5, -60);
      if (mobile) await page.touchscreen.tap(x, y); else await page.mouse.click(x, y);
      await until(() => state(page), s => Number(s.contacts) === 1 && Number(s.x) > 0.1, 'splash wave rocks boat');
      await page.locator('#game-pause').click();
      await until(() => state(page), s => s.paused === 'true', 'pause');
      const frozen = await state(page);
      assert.equal(Number(frozen.placements), 1, 'Tap/click emitted duplicate rings');
      assert.equal(Number(frozen.contacts), 1);
      assert(Number(frozen.y) > -60, 'The river did not carry the boat upward');
      await delay(300);
      const later = await state(page);
      assert.equal(later.ticks, frozen.ticks, 'Paused game advanced');
      assert.equal(later.x, frozen.x, 'Paused boat moved');
      await page.screenshot({path: resolve(output, name + '.png')});
      await page.mouse.click(x, y); await delay(150);
      assert.equal((await state(page)).placements, frozen.placements, 'Paused input created a ripple');

      await page.locator('#game-reset').click();
      await until(() => state(page), s => Number(s.x) === 0 && Number(s.placements) === 0 && Number(s.rings) === 0, 'reset telemetry');
      const reset = await state(page);
      assert.equal(Number(reset.x), 0); assert.equal(Number(reset.y), -60); assert.equal(reset.phase, 'playing'); assert.equal(Number(reset.placements), 0); assert.equal(Number(reset.rings), 0);
      const before = Number(await page.locator('#status').getAttribute('data-frames'));
      await waitFrames(page, before + 2);
      const screenshot = PNG.sync.read(await page.screenshot());
      const boatPoint = (await canvasView(page)).point(0, -60);
      const cx = Math.floor(boatPoint.x * screenshot.width / viewport.width);
      const cy = Math.floor(boatPoint.y * screenshot.height / viewport.height);
      const pixel = (cy * screenshot.width + cx) * 4;
      assert(screenshot.data[pixel] > screenshot.data[pixel + 2] + 15, 'Boat is not visible at the physical center');

      await page.setViewportSize({width: 844, height: 390});
      await delay(200);
      assert.equal(Number((await state(page)).x), 0, 'Resize moved physics');
      await page.locator('#mode').click();
      await until(() => state(page), s => s.enabled === 'false', 'tuning mode');
      assert.equal((await state(page)).enabled, 'false');
      await page.locator('#mode').click();
      await until(() => state(page), s => s.enabled === 'true', 'boat mode');
      assert.equal((await state(page)).enabled, 'true');
      await page.evaluate(() => { Module._OceanSetPaused(0); Module._DriftSetFocused(0); });
      await until(() => state(page), s => s.paused === 'false', 'blur telemetry');
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
      const cancelFrame = Number(await page.locator('#status').getAttribute('data-frames'));
      // A cancellation before the next RAF must discard the queued placement.
      await page.evaluate(() => {
        const canvas = document.getElementById('canvas');
        const rect = canvas.getBoundingClientRect();
        canvas.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true, isPrimary: true,
          button: 0, clientX: rect.left + rect.width / 2, clientY: rect.top + rect.height / 2}));
        canvas.dispatchEvent(new PointerEvent('pointercancel', {bubbles: true, isPrimary: true}));
      });
      await waitFrames(page, cancelFrame + 2);
      assert.equal(Number((await state(page)).placements), 0, 'Cancelled pointer emitted a ripple');
      const {x: rx, y: ry} = (await canvasView(page)).point(5, -60);
      if (mobile) await page.touchscreen.tap(rx, ry); else await page.mouse.click(rx, ry);
      await until(() => state(page), s => Number(s.contacts) === 1 && Number(s.x) < -0.1, 'restart input');
      assert.equal(Number((await state(page)).placements), 1);
      assert.equal(errors.length, 0, errors.join('\n'));
      // Navigate with real pointer strokes; no boat/velocity setters.
      await page.locator('#game-reset').click();
      const placeWorld = async (worldX, worldY) => {
        const point = (await canvasView(page)).point(worldX, worldY);
        if (mobile) await page.touchscreen.tap(point.x, point.y); else await page.mouse.click(point.x, point.y);
      };
      const clearDock = async (worldX, worldY) => {
        const view = await canvasView(page);
        const dock = {...view.point(worldX, worldY), radius: 6 * view.scale};
        for (const selector of ['#status-region', '#game-controls']) {
          const box = await page.locator(selector).boundingBox();
          const closestX = Math.max(box.x, Math.min(dock.x, box.x + box.width));
          const closestY = Math.max(box.y, Math.min(dock.y, box.y + box.height));
          assert(Math.hypot(dock.x - closestX, dock.y - closestY) > dock.radius,
            selector + ' obscures the dock at ' + JSON.stringify(page.viewportSize()));
        }
      };
      const cdp = mobile ? await context.newCDPSession(page) : null;
      const dragWorld = async (from, to) => {
        const view = await canvasView(page);
        const start = view.point(from.x, from.y), end = view.point(to.x, to.y);
        if (mobile) {
          await cdp.send('Input.dispatchTouchEvent', {type: 'touchStart', touchPoints: [{...start, id: 1}]});
          for (let i = 1; i <= 8; ++i) await cdp.send('Input.dispatchTouchEvent', {type: 'touchMove', touchPoints: [{x: start.x + (end.x - start.x) * i / 8, y: start.y + (end.y - start.y) * i / 8, id: 1}]});
          await cdp.send('Input.dispatchTouchEvent', {type: 'touchEnd', touchPoints: []});
        } else {
          await page.mouse.move(start.x, start.y); await page.mouse.down();
          await page.mouse.move(end.x, end.y, {steps: 8}); await page.mouse.up();
        }
      };
      const steer = async (waypoints, terminal, timeout = 300000) => {
        let lastStroke = -30;
        let waypoint = 0;
        const deadline = Date.now() + timeout;
        while (Date.now() < deadline) {
          const boat = await state(page);
          if (boat.phase !== 'playing') {
            assert.equal(boat.phase, terminal, 'Unexpected terminal state');
            return boat;
          }
          const target = waypoints[waypoint];
          const dx = target.x - Number(boat.x), dy = target.y - Number(boat.y);
          const distance = Math.hypot(dx, dy);
          if (distance < 2.5 && waypoint < waypoints.length - 1) { waypoint++; continue; }
          const speed = Math.min(3.2, distance * 0.6);
          const ex = distance > 0.01 ? dx / distance * speed - Number(boat.vx) : -Number(boat.vx);
          const ey = distance > 0.01 ? dy / distance * speed - Number(boat.vy) : -Number(boat.vy);
          const error = Math.hypot(ex, ey);
          // Keep the correction cadence tied to simulation time. Software-GPU
          // frame timing must not change how often this pointer pilot steers.
          if (Number(boat.ticks) - lastStroke >= 30 && error > 0.45) {
            const ux = ex / error, uy = ey / error;
            await dragWorld({x: Number(boat.x) - ux * 3, y: Number(boat.y) - uy * 3},
              {x: Number(boat.x) + ux * 3, y: Number(boat.y) + uy * 3});
            lastStroke = Number(boat.ticks);
          }
          await delay(70);
        }
        throw Error('Timeout: complete course with pointer currents');
      };
      const route = course => {
        const bottom = -72 - course * 16;
        // Cross as soon as the preceding hull clears its gate, leaving room to
        // brake the upward current before the next rock. The rapids need that
        // headroom; waiting farther downstream made CI's touch pilot collide.
        const marks = [[10,27],[10,38],[10,48],[-5,50],[-5,68],[-5,78],
          [course === 2 ? 5 : 10,80],[course === 2 ? 5 : 10,98],[10,108],
          [-8,110],[-8,128],[-8,138],[9,140],[9,158],[9,174]];
        return marks.slice(0, [9,12,15][course]).map(([x,y]) => ({x, y:bottom+y}))
          .concat({x:course===1 ? -9 : 9, y:-bottom-13});
      };
      assert.equal((await state(page)).course, '1');
      assert.equal(await page.evaluate(() => Module._DriftNextCourse()), 0, 'Playing cannot advance');
      assert(await page.locator('#game-next').isHidden());
      await page.setViewportSize(viewport);
      await until(() => state(page), s => Number(s.cameraHeight) < 144, 'scrolling view');
      const startCamera = Number((await state(page)).cameraY);
      const completed = [];
      for (let course = 0; course < 3; ++course) {
        const dock = {x:course===1 ? -9 : 9, y:59+course*16};
        const arrival = await steer(route(course), 'arrived', 900000);
        assert.equal(arrival.course, String(course + 1));
        assert.equal(Number(arrival.docking), 1);
        assert(Math.hypot(Number(arrival.x)-dock.x, Number(arrival.y)-dock.y) <= 4.001);
        assert(Math.hypot(Number(arrival.vx), Number(arrival.vy)) <= 1.5);
        assert(Number(arrival.cameraY) > startCamera + 40, 'Camera did not follow the river');
        await clearDock(dock.x, dock.y);
        await page.screenshot({path:resolve(output, name + '-river-' + (course+1) + '.png')});
        completed.push(arrival);
        await placeWorld(Number(arrival.x)-4, Number(arrival.y)); await delay(100);
        assert.equal((await state(page)).ticks, arrival.ticks, 'Terminal input advanced game');
        if (course < 2) {
          await page.evaluate(() => Module._OceanSetPaused(1));
          await page.locator('#game-next').dblclick();
          await until(() => state(page), s => s.course === String(course+2) && s.phase === 'playing', 'Next course');
          assert.equal((await state(page)).paused, 'false');
          assert(await page.locator('#course-error').isHidden());
          await page.locator('#game-pause').click();
          await until(() => state(page), s => s.paused === 'true', 'pause for Retry');
          await page.locator('#game-reset').click();
          await until(() => state(page), s => Number(s.y) === -60-(course+1)*16 && Number(s.placements) === 0, 'Retry keeps river');
          assert.equal((await state(page)).course, String(course+2));
          await page.locator('#game-pause').click();
        }
        console.log(name + ': river ' + (course+1) + ' rescued');
      }
      assert.equal((await state(page)).complete, 'true');
      assert.equal(await page.locator('#game-next').textContent(), 'Play again');
      await page.evaluate(() => Module._OceanRestart());
      await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'complete graphics restart');
      await waitFrames(page, 3);
      assert.equal((await state(page)).complete, 'true');
      await page.locator('#game-next').click();
      await until(() => state(page), s => s.course === '1' && s.phase === 'playing', 'play again');
      await page.locator('#game-pause').click();
      await until(() => state(page), s => s.paused === 'true', 'pause replay');
      await page.locator('#game-reset').click();
      await until(() => state(page), s => Number(s.y) === -60 && Number(s.placements) === 0, 'replay reset');
      const replay = await state(page);
      assert.equal(Number(replay.rings), 0); assert.equal(replay.complete, 'false');
      assert(await page.locator('#game-next').isHidden());
      // With no correction the steady river carries the boat into the first gate.
      await page.locator('#game-pause').click();
      const crashed = await steer([{x:0,y:59}], 'crashed');
      assert.equal(Number(crashed.crash), 1);
      assert.equal(await page.evaluate(() => Module._DriftNextCourse()), 0);
      await page.locator('#game-reset').click();
      await until(() => state(page), s => s.phase === 'playing' && Number(s.placements) === 0, 'crash Retry');
      assert.equal((await state(page)).course, '1');
      const [learned, arrived, channel] = completed;
      assert.equal(errors.length, 0, errors.join('\n'));
      console.log(name + ': all courses and replay passed');
      report.cases.push({name, status: 'passed', push: frozen, learned, crashed, arrived, channel, replay, errors});
      await context.close(); activePage = undefined;
    }
  }
  assert.deepEqual(report.cases.map(result => result.name), selectedCases, 'Browser shard missed cases');
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
