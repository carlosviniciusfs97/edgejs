#!/usr/bin/env node
// Generates a deterministic synthetic CJS app used by bench-bytecode-cache.sh:
// many small modules plus one large vendor bundle, so engine parse/compile
// time dominates startup and the bytecode-cache effect is measurable.
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OUT_DIR = join(dirname(fileURLToPath(import.meta.url)), 'workloads', 'bytecode-cache', 'gen');
const MODULE_COUNT = 150;
const VENDOR_FUNCTIONS = 2600; // ~1.5 MB of source

// Deterministic PRNG (mulberry32) — the workload must be byte-identical
// across runs so sidecar caches stay valid between benchmark lanes.
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const rand = mulberry32(0xedbe11);

function pick(items) {
  return items[Math.floor(rand() * items.length)];
}

function makeFunction(name) {
  const ops = ['+', '-', '*', '^', '|'];
  const lines = [];
  lines.push(`function ${name}(input, factor) {`);
  lines.push('  let acc = (input | 0) + 1;');
  const steps = 6 + Math.floor(rand() * 6);
  for (let i = 0; i < steps; i++) {
    const op = pick(ops);
    const k = Math.floor(rand() * 0xffff);
    lines.push(`  acc = ((acc ${op} ${k}) ${pick(ops)} (factor | ${i})) & 0x7fffffff;`);
    if (rand() < 0.3) {
      lines.push(`  if (acc % ${2 + Math.floor(rand() * 11)} === 0) { acc = (acc >> 1) + ${k}; }`);
    }
    if (rand() < 0.2) {
      lines.push(`  const slice_${i} = String(acc).split('').reverse().join('');`);
      lines.push(`  acc = (acc + slice_${i}.length) & 0x7fffffff;`);
    }
  }
  lines.push('  return acc;');
  lines.push('}');
  return lines.join('\n');
}

function generateVendor() {
  const parts = ["'use strict';", ''];
  const names = [];
  for (let i = 0; i < VENDOR_FUNCTIONS; i++) {
    const name = `vendorFn${i}`;
    names.push(name);
    parts.push(makeFunction(name));
    parts.push('');
  }
  parts.push('module.exports = {');
  parts.push(`  size: ${names.length},`);
  parts.push('  run(seed) {');
  parts.push('    let acc = seed | 0;');
  // Touch a sample of functions so lazy compilation engines do some real work.
  for (let i = 0; i < VENDOR_FUNCTIONS; i += 50) {
    parts.push(`    acc = vendorFn${i}(acc, ${i});`);
  }
  parts.push('    return acc;');
  parts.push('  },');
  parts.push('};');
  return parts.join('\n');
}

function generateModule(index) {
  const parts = ["'use strict';", ''];
  const deps = [];
  for (let d = 1; d <= 3; d++) {
    const target = index - d * (1 + Math.floor(rand() * 4));
    if (target >= 0) deps.push(target);
  }
  const uniqueDeps = [...new Set(deps)];
  for (const dep of uniqueDeps) {
    parts.push(`const dep${dep} = require('./mod_${String(dep).padStart(3, '0')}.js');`);
  }
  parts.push('');
  const fnCount = 8 + Math.floor(rand() * 5);
  for (let f = 0; f < fnCount; f++) {
    parts.push(makeFunction(`helper${index}_${f}`));
    parts.push('');
  }
  // Memoized: the dep graph is a DAG and compute() must walk it once, not
  // exponentially.
  parts.push('let cachedResult = null;');
  parts.push('module.exports = {');
  parts.push('  compute(seed) {');
  parts.push('    if (cachedResult !== null) return cachedResult;');
  parts.push('    let acc = seed | 0;');
  for (let f = 0; f < fnCount; f++) {
    parts.push(`    acc = helper${index}_${f}(acc, ${index});`);
  }
  for (const dep of uniqueDeps) {
    parts.push(`    acc = (acc + dep${dep}.compute(acc)) & 0x7fffffff;`);
  }
  parts.push('    cachedResult = acc;');
  parts.push('    return acc;');
  parts.push('  },');
  parts.push('};');
  return parts.join('\n');
}

rmSync(OUT_DIR, { recursive: true, force: true });
mkdirSync(OUT_DIR, { recursive: true });

let totalBytes = 0;
const vendor = generateVendor();
writeFileSync(join(OUT_DIR, 'vendor.js'), vendor);
totalBytes += vendor.length;

for (let i = 0; i < MODULE_COUNT; i++) {
  const source = generateModule(i);
  writeFileSync(join(OUT_DIR, `mod_${String(i).padStart(3, '0')}.js`), source);
  totalBytes += source.length;
}

const mainLines = ["'use strict';", "const vendor = require('./vendor.js');"];
for (let i = 0; i < MODULE_COUNT; i++) {
  mainLines.push(`const mod${i} = require('./mod_${String(i).padStart(3, '0')}.js');`);
}
mainLines.push('let acc = vendor.run(1);');
for (let i = 0; i < MODULE_COUNT; i++) {
  mainLines.push(`acc = (acc + mod${i}.compute(acc)) & 0x7fffffff;`);
}
mainLines.push("console.log('checksum:' + acc);");
const main = mainLines.join('\n');
writeFileSync(join(OUT_DIR, 'main.js'), main);
totalBytes += main.length;

console.log(
  `generated ${MODULE_COUNT + 2} files, ${(totalBytes / 1024 / 1024).toFixed(2)} MB at ${OUT_DIR}`,
);
