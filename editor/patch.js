// Keep original name bytes and extra fields (e.g. color) when saving an imported patch.
const encoder = new TextEncoder();
function lines(bytes) {
  const result = [];
  for (let start = 0, i = 0; i <= bytes.length; i++) {
    if (i !== bytes.length && bytes[i] !== 10 && bytes[i] !== 13) continue;
    if (i > start || !result.length) result.push(bytes.slice(start,i));
    if (bytes[i] === 13 && bytes[i+1] === 10) i++;
    start = i+1;
  }
  return result;
}
export function exportPatch(name, values, original) {
  const [first,...rest] = original ? lines(original.bytes) : [];
  const buffer = new Uint8Array(31);
  const {written} = encoder.encodeInto(name, buffer);
  const title = original && name === original.name ? first : buffer.subarray(0,written);
  const decoder = new TextDecoder('ascii');
  const extra = rest.filter(line => {
    const text = decoder.decode(line), param = /^\s*([+-]?\d+)\s*,/.exec(text);
    return !/^ver=/.test(text) && !(param && Number(param[1]) >= 0 && Number(param[1]) < values.length);
  });
  return new Blob([title, '\r\n', ...extra.flatMap(line => [line,'\r\n']),
    'ver=' + Math.max(112,original?.version || 0) + '\r\n',
    ...values.map((value,i) => i + ',' + value + '\r\n')], {type:'application/octet-stream'});
}
