// Validates the LeviFSR AI shaders with glslang (the reference GLSL/ESSL front
// end).
//
// The shaders are written for OpenGL ES 3.0 (`#version 300 es`), which is what
// a Minecraft Bedrock process on Android provides. glslang's WASM build only
// emits SPIR-V, and SPIR-V imposes rules that do not exist in OpenGL ES:
//
//   * ESSL 310 instead of 300 es
//   * explicit locations on user in/out variables
//   * gl_VertexIndex instead of gl_VertexID
//   * explicit bindings on samplers
//   * plain uniforms inside a uniform block
//
// This script applies that mechanical adaptation *for validation only* and
// then compiles the real shader body. It therefore proves that the GLSL the
// module ships is well formed; it does not modify the shipped shader.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import glslangModule from '@webgpu/glslang/dist/web-devel/glslang.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const wasmPath =
    path.join(here, 'node_modules/@webgpu/glslang/dist/web-devel/glslang.wasm');

const realFetch = globalThis.fetch;
globalThis.fetch = async (input, init) => {
  const url = typeof input === 'string' ? input : input.url;
  if (url.endsWith('glslang.wasm')) {
    return new Response(fs.readFileSync(wasmPath),
                        {headers: {'content-type': 'application/wasm'}});
  }
  return realFetch(input, init);
};

function stripComments(code) {
  return code.split('\n').map((line) => {
    const index = line.indexOf('//');
    return index === -1 ? line : line.slice(0, index).trimEnd();
  }).join('\n');
}

function adaptForSpirv(source) {
  let code = stripComments(source)
      .replace(/#version\s+300\s+es/, '#version 310 es')
      .replace(/gl_VertexID/g, 'gl_VertexIndex');

  let binding = 0;
  code = code.replace(/^uniform\s+(sampler2D)\s+(\w+)\s*;/gm,
                      (_m, type, name) =>
                          `layout(binding = ${binding++}) uniform ${type} ${name};`);

  const plain = [...code.matchAll(/^uniform\s+(\w+)\s+(\w+)\s*;/gm)];
  code = code.replace(/^uniform\s+\w+\s+\w+\s*;$/gm, '');
  if (plain.length > 0) {
    const block =
        `layout(binding = 30) uniform LeviFsrParams {\n` +
        plain.map((m) => `  ${m[1]} ${m[2]};`).join('\n') + `\n};\n`;
    // The block has to come after #version and the precision statements.
    const lines = code.split('\n');
    let insertAt = 0;
    for (let i = 0; i < lines.length; ++i) {
      if (/^#version\b/.test(lines[i]) || /^precision\b/.test(lines[i])) {
        insertAt = i + 1;
      }
    }
    lines.splice(insertAt, 0, block);
    code = lines.join('\n');
  }

  code = code
      .replace(/^out\s+vec2\s+vUv\s*;/m, 'layout(location = 0) out vec2 vUv;')
      .replace(/^in\s+vec2\s+vUv\s*;/m, 'layout(location = 0) in vec2 vUv;')
      .replace(/^out\s+vec4\s+fragColor\s*;/m,
               'layout(location = 0) out vec4 fragColor;');

  return code;
}

const glslang = await glslangModule();
const files = process.argv.slice(2);
let failed = 0;

for (const file of files) {
  const source = fs.readFileSync(file, 'utf8');
  const match = source.match(/=\s*R"\(([\s\S]*?)\)";/);
  const code = match ? match[1] : source;
  const stage = file.includes('Vertex') ? 'vertex' : 'fragment';
  try {
    const spirv = glslang.compileGLSL(adaptForSpirv(code), stage, false);
    console.log(`OK   ${path.basename(file)}  (${stage}, ${spirv.length} SPIR-V words)`);
  } catch (error) {
    failed++;
    console.log(`FAIL ${path.basename(file)}  (${stage})`);
    console.log(String(error).split('\n').slice(0, 14).join('\n'));
    if (process.env.DUMP === '1') {
      console.log('--- adapted source ---');
      console.log(adaptForSpirv(code));
    }
  }
}

process.exit(failed === 0 ? 0 : 1);
