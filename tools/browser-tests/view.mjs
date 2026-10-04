import assert from 'node:assert/strict';

export async function canvasView(page) {
  const box = await page.locator('#canvas').boundingBox();
  assert(box && box.width > 0 && box.height > 0, 'Canvas has no usable extent');
  const camera = await page.locator('#game-status').evaluate(el => ({...el.dataset}));
  const height = Number(camera.cameraHeight);
  const centerX = Number(camera.cameraX), centerY = Number(camera.cameraY);
  assert(height > 0 && Number.isFinite(centerY), 'No presented camera');
  const scale = box.height / height;
  const point = (x, y) => ({x: box.x + box.width / 2 + (x - centerX) * scale,
    y: box.y + box.height / 2 - (y - centerY) * scale});
  return {box, height, scale, point};
}
