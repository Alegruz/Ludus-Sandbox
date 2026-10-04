import assert from 'node:assert/strict';

// Match game::FitCamera using the actual canvas, which excludes mobile HUD space.
export async function canvasView(page) {
  const box = await page.locator('#canvas').boundingBox();
  assert(box && box.width > 0 && box.height > 0, 'Canvas has no usable extent');
  const height = Math.max(90, 70 * box.height / box.width);
  const scale = box.height / height;
  const point = (x, y) => ({x: box.x + box.width / 2 + x * scale,
    y: box.y + box.height / 2 - y * scale});
  return {box, height, scale, point};
}
