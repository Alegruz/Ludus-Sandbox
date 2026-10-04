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
  rafDelayMs: {mouse: 250, touch: 250},
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
      if (process.env.DRIFT_GAME_FILTER && !name.includes(process.env.DRIFT_GAME_FILTER)) continue;
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
      const viewHeight = Math.max(90, 70 / (viewport.width / viewport.height));
      const x = viewport.width / 2 - 5 * viewport.height / viewHeight;
      const y = viewport.height / 2 + 22 * viewport.height / viewHeight;
      if (mobile) await page.touchscreen.tap(x, y); else await page.mouse.click(x, y);
      await until(() => state(page), s => Number(s.contacts) === 1 && Number(s.x) > 0.1, 'splash wave rocks boat');
      await page.locator('#game-pause').click();
      await until(() => state(page), s => s.paused === 'true', 'pause');
      const frozen = await state(page);
      assert.equal(Number(frozen.placements), 1, 'Tap/click emitted duplicate rings');
      assert.equal(Number(frozen.contacts), 1);
      assert(Math.abs(Number(frozen.y) + 22) < 0.08, 'Push direction/mapping is incorrect');
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
      assert.equal(Number(reset.x), 0); assert.equal(Number(reset.y), -22); assert.equal(reset.phase, 'playing'); assert.equal(Number(reset.placements), 0); assert.equal(Number(reset.rings), 0);
      const before = Number(await page.locator('#status').getAttribute('data-frames'));
      await waitFrames(page, before + 2);
      const screenshot = PNG.sync.read(await page.screenshot());
      const cx = Math.floor(screenshot.width / 2), cy = Math.floor(screenshot.height / 2 + 22 * screenshot.height / viewHeight);
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
      const rx = 844 / 2 + 5 * 390 / 90, ry = 390 / 2 + 22 * 390 / 90;
      if (mobile) await page.touchscreen.tap(rx, ry); else await page.mouse.click(rx, ry);
      await until(() => state(page), s => Number(s.contacts) === 1 && Number(s.x) < -0.1, 'restart input');
      assert.equal(Number((await state(page)).placements), 1);
      assert.equal(errors.length, 0, errors.join('\n'));
      // Navigate with real pointer strokes; no boat/velocity setters.
      await page.locator('#game-reset').click();
      const placeWorld = async (worldX, worldY) => {
        const size = page.viewportSize();
        const height = Math.max(90, 70 / (size.width / size.height));
        const px = size.width / 2 + worldX * size.height / height;
        const py = size.height / 2 - worldY * size.height / height;
        if (mobile) await page.touchscreen.tap(px, py); else await page.mouse.click(px, py);
      };
      const clearDock = async (worldX, worldY) => {
        const size = page.viewportSize();
        const scale = size.height / Math.max(90, 70 / (size.width / size.height));
        const box = await page.locator('#status-region').boundingBox();
        const dock = {x: size.width / 2 + worldX * scale, y: size.height / 2 - worldY * scale, radius: 5 * scale};
        const closestX = Math.max(box.x, Math.min(dock.x, box.x + box.width));
        const closestY = Math.max(box.y, Math.min(dock.y, box.y + box.height));
        assert(Math.hypot(dock.x - closestX, dock.y - closestY) > dock.radius,
          'HUD obscures the dock at ' + JSON.stringify(size));
      };
      const cdp = mobile ? await context.newCDPSession(page) : null;
      const dragWorld = async (from, to) => {
        const size = page.viewportSize();
        const height = Math.max(90, 70 / (size.width / size.height));
        const screen = p => ({x: size.width / 2 + p.x * size.height / height, y: size.height / 2 - p.y * size.height / height});
        const start = screen(from), end = screen(to);
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
        let lastStroke = 0;
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
          if (Date.now() - lastStroke > 500 && error > 0.45) {
            const ux = ex / error, uy = ey / error;
            await dragWorld({x: Number(boat.x) - ux * 3, y: Number(boat.y) - uy * 3},
              {x: Number(boat.x) + ux * 3, y: Number(boat.y) + uy * 3});
            lastStroke = Date.now();
          }
          await delay(70);
        }
        throw Error('Timeout: complete course with pointer currents');
      };
      assert.equal((await state(page)).course, '1');
      assert.equal(await page.evaluate(() => Module._DriftNextCourse()), 0, 'Playing cannot advance');
      assert(await page.locator('#game-next').isHidden());
      const learned = await steer([{x: 8, y: 10}], 'arrived');
      assert.equal(Number(learned.docking), 1);
      assert(Math.hypot(Number(learned.x) - 8, Number(learned.y) - 10) <= 3.001);
      await clearDock(8, 10);
      await page.screenshot({path: resolve(output, name + '-first-course.png')});
      await page.evaluate(() => Module._OceanSetPaused(1));
      await until(() => state(page), s => s.paused === 'true', 'pause before Next');
      await page.locator('#game-next').dblclick();
      await until(() => state(page), s => s.course === '2' && s.phase === 'playing', 'next level');
      assert.equal((await state(page)).y, '-22');
      assert.equal((await state(page)).paused, 'false', 'Next left the new course paused');
      assert(await page.locator('#course-error').isHidden(), 'Rapid Next clicks produced a spurious error');
      assert.equal(Number((await state(page)).placements), 0, 'Transition emitted a ripple');
      console.log(name + ': first course complete');
      await placeWorld(0, 0);
      await until(() => state(page), s => Number(s.result) === 5, 'solid rock rejects ripple origin');
      const rejected = await state(page);
      assert.equal(Number(rejected.placements), 0);
      assert.equal(Number(rejected.rings), 0);
      assert.equal(Number(rejected.cooldown), 0, 'Rock placement consumed cooldown');
      // A straight approach hits the rock; terminal input freezes until Retry.
      const crashed = await steer([{x: 0, y: 24}], 'crashed');
      assert.equal(Number(crashed.crash), 1);
      assert.equal(await page.evaluate(() => Module._DriftNextCourse()), 0, 'Crashed cannot advance');
      assert(await page.locator('#game-next').isHidden());
      await placeWorld(-5, -8); await delay(250);
      assert.equal((await state(page)).ticks, crashed.ticks);
      assert.equal((await state(page)).placements, crashed.placements);
      await page.screenshot({path: resolve(output, name + '-crashed.png')});
      await page.locator('#game-reset').click();
      await until(() => state(page), s => s.phase === 'playing' && Number(s.placements) === 0, 'retry after crash');
      assert.equal((await state(page)).course, '2', 'Retry changed the course');
      const arrived = await steer([{x: 12, y: -22}, {x: 12, y: 24}, {x: 0, y: 24}], 'arrived');
      assert.equal(Number(arrived.docking), 1);
      assert(Math.hypot(Number(arrived.x), Number(arrived.y) - 24) <= 3.001, 'Hull is not contained in the dock');
      assert(Math.hypot(Number(arrived.vx), Number(arrived.vy)) <= 1.5, 'Arrived too fast');
      await clearDock(0, 24);
      await page.screenshot({path: resolve(output, name + '-arrived.png')});
      await page.evaluate(() => Module._OceanRestart());
      await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'terminal graphics restart');
      assert.equal((await state(page)).phase, 'arrived');
      assert.equal((await state(page)).course, '2', 'Graphics restart changed the course');
      await page.locator('#game-next').click();
      await until(() => state(page), s => s.course === '3' && s.phase === 'playing', 'channel level');
      assert.equal(Number((await state(page)).y), -28);
      assert.equal(Number((await state(page)).rings), 0);
      await page.locator('#game-reset').click();
      await until(() => state(page), s => s.course === '3' && Number(s.placements) === 0, 'channel retry');
      console.log(name + ': rock course complete');
      // Portrait and landscape must keep the authored hazards/dock in view.
      await page.setViewportSize(viewport);
      await delay(200);
      await clearDock(10, 29);
      const channel = await steer([{x: 12, y: -28}, {x: 12, y: -12}, {x: 0, y: -2},
        {x: -12, y: 7}, {x: -12, y: 12}, {x: 12, y: 18}, {x: 10, y: 29}], 'arrived', 600000);
      assert.equal(channel.complete, 'true');
      assert(Math.hypot(Number(channel.x) - 10, Number(channel.y) - 29) <= 3.001);
      assert(Math.hypot(Number(channel.vx), Number(channel.vy)) <= 1.5);
      assert.equal(await page.locator('#game-next').textContent(), 'Play again');
      assert.match(await page.locator('#game-status').textContent(), /All three courses complete/);
      await clearDock(10, 29);
      await page.screenshot({path: resolve(output, name + '-complete.png')});
      await page.evaluate(() => Module._OceanRestart());
      await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'complete graphics restart');
      await waitFrames(page, 3);
      assert.equal((await state(page)).complete, 'true');
      await page.locator('#game-next').click();
      await until(() => state(page), s => s.course === '1' && s.phase === 'playing', 'play again');
      const replay = await state(page);
      assert.equal(Number(replay.x), 0); assert.equal(Number(replay.y), -22);
      assert.equal(Number(replay.placements), 0); assert.equal(Number(replay.rings), 0);
      assert.equal(Number(replay.energy), 0); assert.equal(Number(replay.height), 0);
      assert.equal(replay.complete, 'false');
      assert(await page.locator('#game-next').isHidden());
      assert.equal(errors.length, 0, errors.join('\n'));
      console.log(name + ': all courses and replay passed');
      report.cases.push({name, status: 'passed', push: frozen, learned, crashed, arrived, channel, replay, errors});
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
