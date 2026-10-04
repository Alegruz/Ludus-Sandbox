// Mobile HUD, modal input ownership and real touch on the packaged game.
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, sep} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {createHash} from 'node:crypto';
import {chromium} from 'playwright';
import {canvasView} from './view.mjs';

const root = resolve(process.argv[2]), output = resolve(process.argv[3]);
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
const report = {kind: 'Software GPU and emulated touch', rafDelayMs: 500, payload: info.sha256, cases: []};
const state = page => page.locator('#game-status').evaluate(el => ({...el.dataset}));
async function until(read, test, label) {
  const end = Date.now() + 45000;
  while (Date.now() < end) { const v = await read(); if (test(v)) return v; await delay(50); }
  throw Error('Timeout: ' + label);
}
async function frames(page) {
  const first = Number(await page.locator('#status').getAttribute('data-frames'));
  await until(() => page.locator('#status').getAttribute('data-frames'), n => Number(n) >= first + 3, 'rendered layout');
}
const intersects = (a, b) => a.x < b.x + b.width && a.x + a.width > b.x &&
  a.y < b.y + b.height && a.y + a.height > b.y;
async function clearView(page) {
  const view = await canvasView(page), size = page.viewportSize();
  assert(await page.locator('#game-help').isHidden());
  assert(await page.locator('#game-instructions').isHidden());
  assert(await page.locator('#status').isHidden(), 'Ready message still consumes HUD space');
  for (const selector of ['#status-region', '#game-controls']) {
    const box = await page.locator(selector).boundingBox();
    assert(!intersects(box, view.box), selector + ' covers the canvas');
    assert(box.x >= 0 && box.y >= 0 && box.x + box.width <= size.width && box.y + box.height <= size.height);
  }
  for (const button of await page.locator('#game-controls button:visible').all()) {
    const box = await button.boundingBox();
    assert(box.width >= 44 && box.height >= 44, 'Touch target too small');
  }
  const geometry = await page.evaluate(() => ({scrollX, scrollY,
    width: document.documentElement.scrollWidth, height: document.documentElement.scrollHeight}));
  assert.equal(geometry.scrollX, 0); assert.equal(geometry.scrollY, 0);
  assert(geometry.width <= size.width && geometry.height <= size.height, 'HUD causes page scrolling');
  return view;
}
let browser, active;
try {
  browser = await chromium.launch({headless: true, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan',
    '--use-webgpu-adapter=swiftshader', '--use-angle=swiftshader', '--use-vulkan=swiftshader',
    '--disable-vulkan-surface', '--enable-unsafe-swiftshader']});
  for (const backend of ['webgpu', 'webgl2']) {
    const context = await browser.newContext({viewport: {width: 390, height: 660}, hasTouch: true,
      deviceScaleFactor: Number(process.env.DRIFT_UI_SCALE || 1)});
    context.setDefaultTimeout(60000);
    await context.addInitScript(() => {
      const raf = requestAnimationFrame;
      window.requestAnimationFrame = callback => raf(time => setTimeout(() => callback(time), 500));
    });
    const page = await context.newPage(); active = page;
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}/?backend=${backend}`);
    await until(() => page.locator('#status').getAttribute('data-state'), s => s === 'playing', 'startup');
    // Freeze through the existing blur path for representative ambient screenshots;
    // the ordinary playing HUD stays visible, rather than a pause feedback card.
    await page.evaluate(() => Module._DriftSetFocused(0));
    for (const [width, height] of [[390,660], [320,568], [844,390], [320,360]]) {
      await page.setViewportSize({width, height}); await frames(page);
      const view = await clearView(page);
      await page.screenshot({path: resolve(output, `${backend}-${width}x${height}.png`)});
      report.cases.push({backend, width, height, canvas: view.box, status: 'passed'});
    }
    await page.setViewportSize({width: 390, height: 660});
    await page.evaluate(() => {
      document.documentElement.style.setProperty('--safe-top', '24px');
      document.documentElement.style.setProperty('--safe-bottom', '20px');
      document.documentElement.style.setProperty('--safe-left', '12px');
      document.documentElement.style.setProperty('--safe-right', '12px');
    });
    await frames(page);
    const safe = await clearView(page);
    assert(safe.box.x >= 12 && safe.box.y >= 24 && safe.box.x + safe.box.width <= 378);
    await page.screenshot({path: resolve(output, backend + '-safe-area.png')});
    await page.locator('#mode').click();
    await until(() => state(page), s => s.enabled === 'false', 'ocean mode');
    assert.equal(await page.locator('#mode').textContent(), 'Rescue');
    await page.locator('#help-toggle').click();
    assert(await page.locator('#game-instructions').isVisible(), 'Ocean help has no instructions');
    assert.match(await page.locator('#game-instructions').textContent(), /Drag to build a current/);
    await page.getByRole('button', {name: 'Back to ocean'}).click();
    await page.locator('#panel summary').click();
    const panel = await page.locator('#panel').boundingBox();
    const header = await page.locator('#status-region').boundingBox();
    assert(panel.y >= header.y + header.height && panel.y + panel.height <= 640, 'Tuning panel escapes safe area');
    await page.screenshot({path: resolve(output, backend + '-tuning.png')});
    await page.locator('#mode').click();
    await until(() => state(page), s => s.enabled === 'true', 'boat mode');
    await page.evaluate(() => { Module._DriftSetFocused(1); Module._OceanSetWaveIntensity(0); });
    await frames(page); await page.locator('#game-reset').click();
    await until(() => state(page), s => Number(s.energy) === 0, 'calm reset');
    const view = await canvasView(page), tap = view.point(-5, -22);
    await page.touchscreen.tap(tap.x, tap.y);
    await until(() => state(page), s => Number(s.placements) === 1, 'touch mapping on inset canvas');
    await page.locator('#help-toggle').click();
    await until(() => state(page), s => s.paused === 'true', 'help pauses');
    assert(await page.locator('#game-help').isVisible());
    assert.equal(await page.locator('#help-toggle').getAttribute('aria-expanded'), 'true');
    const paused = await state(page); await frames(page);
    assert.equal((await state(page)).ticks, paused.ticks, 'Help advances the boat');
    assert.equal((await state(page)).placements, paused.placements, 'Help button adds a splash');
    assert(await page.locator('#game-help').evaluate(el => el.contains(document.activeElement)), 'Modal has no focus');
    await page.screenshot({path: resolve(output, backend + '-help.png')});
    await page.getByRole('button', {name: 'Back to ocean'}).click();
    await until(() => state(page), s => s.paused === 'false', 'help restores running state');
    await page.locator('#game-pause').click();
    await until(() => state(page), s => s.paused === 'true', 'manual pause');
    await page.locator('#help-toggle').click(); await page.keyboard.press('Escape');
    await until(() => page.locator('#game-help').isVisible(), v => !v, 'Escape closes help');
    await frames(page); assert.equal((await state(page)).paused, 'true', 'Help overrides manual pause');
    await page.evaluate(() => Module._OceanRestart()); await frames(page);
    await clearView(page);
    assert.equal(errors.length, 0, errors.join('\n'));
    report.cases.push({backend, stateChecks: 'safe area, tuning, touch mapping, help pause/focus/Escape, restart', status: 'passed'});
    console.log(backend + ': mobile HUD passed');
    await context.close(); active = undefined;
  }
} catch (error) {
  report.failure = String(error); process.exitCode = 1;
  if (active) await active.screenshot({path: resolve(output, 'failure.png')}).catch(() => {});
} finally {
  await browser?.close(); server.close();
  await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
}
