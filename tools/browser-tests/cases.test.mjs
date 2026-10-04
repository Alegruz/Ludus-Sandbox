import assert from 'node:assert/strict';
import {test} from 'node:test';
import {selectBrowserCases} from './cases.mjs';

test('the default suite covers both backends and both pointer types', () => {
  assert.deepEqual(selectBrowserCases(), ['webgpu-mouse', 'webgpu-touch', 'webgl2-mouse', 'webgl2-touch']);
});

test('four CI shards cover the full suite exactly once', () => {
  const selected = ['webgpu-mouse', 'webgpu-touch', 'webgl2-mouse', 'webgl2-touch']
    .flatMap(filter => selectBrowserCases(filter));
  assert.deepEqual(selected, selectBrowserCases());
});

test('developer backend filters remain supported and empty shards fail', () => {
  assert.deepEqual(selectBrowserCases('webgpu'), ['webgpu-mouse', 'webgpu-touch']);
  assert.throws(() => selectBrowserCases('webgpu-typo'), /No browser cases match/);
});
