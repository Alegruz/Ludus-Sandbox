// Keep browser shards exhaustive, and reject filters that would pass no tests.
const cases = ['webgpu-mouse', 'webgpu-touch', 'webgl2-mouse', 'webgl2-touch'];

export function selectBrowserCases(filter = '') {
  const selected = cases.filter(name => name.includes(filter));
  if (selected.length === 0) throw Error('No browser cases match filter: ' + filter);
  return selected;
}
