// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 abdulahmad
// Exports the box mockup's 3D scenes (three.js r150) to the box module's scene files.
// Usage: node export.js <mockup dir (index src/, models/, art-private/)> <out dir>
// The mockup's own scene code is loaded in headless Chromium with a few lines added that hand its objects out.
// Writes <out>/<game>/scene.txt (nodes, meshes, materials, textures, lights), scene.vtx (vertices), scene.idx (indices) and the
// procedural textures (tex/*.png). Retail scans are NOT written: a texture made from a scan is written as the
// scan's key, which the module loads at run time from the player's own art folder.
const fs = require('fs'), path = require('path');
const { chromium } = require('playwright');
const M = path.resolve(process.argv[2] || '.'), OUT = path.resolve(process.argv[3] || 'out');
const rd = p => fs.readFileSync(path.join(M, p), 'utf8');

function dataUri(p) {
  const b = fs.readFileSync(path.join(M, p));
  return 'data:' + (p.endsWith('png') ? 'image/png' : 'image/jpeg') + ';base64,' + b.toString('base64');
}
// the art keys of the mockup's build.py; value = file under art-private (the run-time art folder uses the same names)
const A = 'art-private/pc94/', G = 'art-private/gen94/web/';
const ART = {
  sleeve: A + 'sleeve_front.jpg', lid: A + 'inner_lid_front.jpg', leaflet: A + 'web/leaflet.jpg',
  sleeveSpine: A + 'web/sleeve_spine.jpg', sleeveTop: A + 'web/sleeve_top.jpg', sleeveBottom: A + 'web/sleeve_bottom.jpg',
  lidSpine: A + 'web/lid_spine.jpg', lidRight: A + 'web/lid_right.jpg', lidTop: A + 'web/lid_top.jpg', lidBottom: A + 'web/lid_bottom.jpg',
  baseBottom: A + 'web/base_bottom.jpg', guideSpine: A + 'web/guide_spine.jpg', bundleBand: A + 'web/bundle_band.png', bundleBase: A + 'web/bundle_base.png',
  lineupOut: A + 'web/lineup_out.jpg', lineOutFront: A + 'web/line_out_front.jpg', lineOutBack: A + 'web/line_out_back.jpg',
  lineInLeft: A + 'web/line_in_left.jpg', lineInRight: A + 'web/line_in_right.jpg', lineup: A + 'web/lineup.jpg',
  regfront: A + 'web/regfront.jpg', regback: A + 'web/regback.jpg', blank: A + 'web/blank.jpg',
  genFront: G + 'box_front.jpg', genSpine: G + 'box_spine.jpg', genBack: G + 'box_back.jpg', genCart: G + 'cart.jpg',
  genCartFlat: 'models/cart_label.jpg', genManual: G + 'manual.jpg',
};
for (let i = 1; i <= 4; i++) ART['disk' + i] = A + 'disk' + i + '.png';
const man = fs.readdirSync(path.join(M, A + 'web')).filter(f => /^man-\d+\.jpg$/.test(f)).sort();
const tech = fs.readdirSync(path.join(M, A + 'web')).filter(f => /^tech-\d+\.jpg$/.test(f)).sort();
man.forEach((f, i) => ART['man' + i] = A + 'web/' + f);
tech.forEach((f, i) => ART['tech' + i] = A + 'web/' + f);

// the larger scans in art-private/pc94/hires/<key>.<ext> (made from the full-size layouts) win over the web-sized ones
for (const k in ART) {
  if (!ART[k].startsWith(A)) continue;
  for (const ext of ['.jpg', '.png']) if (fs.existsSync(path.join(M, A + 'hires/' + k + ext))) { ART[k] = A + 'hires/' + k + ext; break; }
}
const IMG = {};
for (const k in ART) if (!/^(man|tech)\d/.test(k)) IMG[k] = dataUri(ART[k]);
IMG.man = man.map((f, i) => dataUri(ART['man' + i]));
IMG.tech = tech.map((f, i) => dataUri(ART['tech' + i]));

function patch(src, from, to) { if (!src.includes(from)) throw new Error('patch point missing: ' + from.slice(0, 60)); return src.replace(from, to); }
let pc = rd('src/pc3d.js');
pc = patch(pc, 'const B={root,g,N,w,flip:0,leaves,blk,t};', 'const B={root,g,N,w,flip:0,leaves,blk,t,bc,sp};');
pc = patch(pc, 'const F={root,N:2,flip:0};', 'const F={root,N:2,flip:0,A,Bp,g};');
pc = patch(pc, 'const F={root,N:1,flip:0};', 'const F={root,N:1,flip:0,g};');
pc = patch(pc, 'draw(null);if(src){', 'draw(null);t.userData.notch={src,w,h,NR,NY,cream:CREAM};if(src){');
pc = patch(pc, 'return {enter,say:', 'window.__PC={get scene(){return scene},get rig(){return rig},get pair(){return pair},get sleeve(){return sleeve},get guide(){return guide},get gBook(){return gBook},get inbox(){return inbox},get lid(){return lid},get tray(){return tray},get trayItems(){return trayItems},get baseItems(){return baseItems},get techBook(){return techBook},get regFlip(){return regFlip},get lineBook(){return lineBook},get diskItems(){return diskItems},get pcDesk(){return pcDesk},get pcParts(){return pcParts},get cam(){return cam}};return {enter,say:');
let gen = rd('src/gen3d.js');
gen = patch(gen, 'return {enter,back,state', 'window.__GEN={get scene(){return scene},get caseG(){return caseG},get cart(){return cart},get tv(){return tv},get tvScreen(){return tvScreen},get tvKnob(){return tvKnob},get con(){return con},get pads(){return pads},get tap(){return tap},get CP(){return CP},get NP(){return NP},get cam(){return cam}};return {enter,back,state');

let html = rd('src/src.html');
html = html.replace('/*THREE*/', () => rd('src/three.min.js')).replace('/*PC3D*/', () => pc)
  .replace('/*GENMODELS*/', () => rd('models/genmodels.js')).replace('/*GEN3D*/', () => gen)
  .replace('/*IMG*/', () => 'const IMG=' + JSON.stringify(IMG) + ';\nconst FILELIST={};');
fs.mkdirSync(OUT, { recursive: true });
// which scan each art key is (make-art.py copies them into a player's art folder under the key's name)
fs.writeFileSync(path.join(OUT, 'art-map.txt'), Object.keys(ART).map(k => k + ' ' + ART[k]).join('\n') + '\n');
const pagePath = path.join(OUT, '_export_page.html');
fs.writeFileSync(pagePath, html);

// in the page: walk a scene, name the objects the module drives, write out everything
function dump(which, uriKey) {
  const T = THREE, W = which === 'pc' ? window.__PC : window.__GEN;
  const role = new Map();
  const name = (o, n) => { if (o && !role.has(o)) role.set(o, n); };
  if (which === 'pc') {
    ['rig', 'pair', 'sleeve', 'guide', 'inbox', 'lid', 'tray', 'trayItems'].forEach(k => name(W[k], k));
    const book = (B, n) => { name(B.root, n); name(B.g, n + '.g'); B.leaves.forEach((L, i) => name(L, n + '.leaf' + i)); name(B.blk, n + '.blk'); name(B.bc, n + '.bc'); name(B.sp, n + '.sp'); };
    book(W.gBook, 'gBook'); book(W.techBook, 'techBook');
    name(W.lineBook.root, 'lineBook'); name(W.lineBook.g, 'lineBook.g'); name(W.lineBook.A, 'lineBook.A'); name(W.lineBook.Bp, 'lineBook.B');
    name(W.regFlip.root, 'regFlip'); name(W.regFlip.g, 'regFlip.g');
    W.baseItems.forEach(m => name(m, 'item.' + m.userData.id));
    // the PC on the desk (the install)
    name(W.pcDesk, 'pcDesk');
    for (const k in W.pcParts) { if (Array.isArray(W.pcParts[k])) W.pcParts[k].forEach((o, i) => name(o, 'pc.' + k.replace(/s$/, '') + (i + 1))); else name(W.pcParts[k], 'pc.' + k); }
  } else {
    ['caseG', 'cart', 'tv', 'tvScreen', 'tvKnob', 'con', 'tap'].forEach(k => name(W[k], k));
    W.pads.forEach((p, i) => name(p, 'pad' + i));
    for (const k in W.CP) name(W.CP[k], 'case.' + k);
    for (const k in W.NP) { if (Array.isArray(W.NP[k])) W.NP[k].forEach((o, i) => name(o, 'con.' + k + i)); else name(W.NP[k], 'con.' + k); }
  }
  const scene = W.scene; scene.updateMatrixWorld(true);
  const nodes = [], meshes = [], mats = [], texs = [], lights = [], verts = [], idx = [];
  const matIx = new Map(), texIx = new Map();
  const texOf = t => {
    if (!t) return -1;
    if (texIx.has(t)) return texIx.get(t);
    const e = { wrapS: t.wrapS === T.RepeatWrapping ? 1 : 0, wrapT: t.wrapT === T.RepeatWrapping ? 1 : 0, off: [t.offset.x, t.offset.y], rep: [t.repeat.x, t.repeat.y], flipY: t.flipY ? 1 : 0 };
    const img = t.image;
    if (t.userData && t.userData.notch) {
      const n = t.userData.notch; e.kind = 'notch'; e.key = n.src ? uriKey[n.src] : ''; e.w = n.w; e.h = n.h; e.nr = n.NR; e.ny = n.NY; e.cream = n.cream;
    } else if (img && img.src && uriKey[img.src]) { e.kind = 'art'; e.key = uriKey[img.src]; }
    else if (img && (img.tagName === 'CANVAS' || img.getContext)) { e.kind = 'png'; e.png = img.toDataURL('image/png'); }
    else if (img && img.src && img.src.startsWith('data:image/svg')) { e.kind = 'svg'; e.src = img.src; }
    else { e.kind = 'none'; }
    texIx.set(t, texs.length); texs.push(e); return texs.length - 1;
  };
  const matOf = m => {
    if (matIx.has(m)) return matIx.get(m);
    const c = m.color || new T.Color(1, 1, 1);
    const e = { type: m.isMeshBasicMaterial ? 'B' : m.isMeshPhongMaterial ? 'P' : 'L', color: [c.r, c.g, c.b], map: texOf(m.map), opacity: m.opacity,
      transparent: m.transparent ? 1 : 0, alphaTest: m.alphaTest || 0, side: m.side, depthWrite: m.depthWrite ? 1 : 0, visible: m.visible ? 1 : 0,
      spec: m.specular ? [m.specular.r, m.specular.g, m.specular.b] : [0, 0, 0], shin: m.shininess || 0,
      emis: m.emissive ? [m.emissive.r, m.emissive.g, m.emissive.b] : [0, 0, 0], poff: m.polygonOffset ? 1 : 0,
      vcol: m.vertexColors ? 1 : 0 };
    matIx.set(m, mats.length); mats.push(e); return mats.length - 1;
  };
  const nodeIx = new Map();
  scene.traverse(o => {
    const i = nodes.length; nodeIx.set(o, i);
    const ud = o.userData || {};
    nodes.push({ parent: o.parent ? nodeIx.get(o.parent) : -1, name: role.get(o) || '', id: typeof ud.id === 'string' ? ud.id : '', label: typeof ud.label === 'string' ? ud.label : '',
      pos: o.position.toArray(), rot: [o.rotation.x, o.rotation.y, o.rotation.z], order: o.rotation.order, scale: o.scale.toArray(), vis: o.visible ? 1 : 0,
      onX: ud.onX, offX: ud.offX });
    if (o.isLight) {
      const l = { node: i, kind: o.isAmbientLight ? 'A' : o.isHemisphereLight ? 'H' : o.isDirectionalLight ? 'D' : o.isPointLight ? 'P' : '?', color: o.color.toArray(), I: o.intensity,
        ground: o.groundColor ? o.groundColor.toArray() : [0, 0, 0], pos: o.getWorldPosition(new T.Vector3()).toArray(),
        target: o.target ? o.target.getWorldPosition(new T.Vector3()).toArray() : [0, 0, 0] };
      lights.push(l);
    }
    if (o.isMesh) {
      let g = o.geometry; if (g.index) g = g.toNonIndexed();
      const P = g.attributes.position, N = g.attributes.normal, U = g.attributes.uv, C = g.attributes.color;
      if (!N) { g.computeVertexNormals(); }
      const NN = g.attributes.normal;
      const groups = g.groups.length ? g.groups : [{ start: 0, count: P.count, materialIndex: 0 }];
      for (const gr of groups) {
        const m = Array.isArray(o.material) ? o.material[gr.materialIndex] : o.material;
        if (!m) continue;
        const first = verts.length / 8, ifirst = idx.length, seen = new Map();
        const end = Math.min(P.count, gr.start + gr.count);
        for (let v = gr.start; v < end; v++) {
          const a = [P.getX(v), P.getY(v), P.getZ(v), NN.getX(v), NN.getY(v), NN.getZ(v), U ? U.getX(v) : 0, U ? U.getY(v) : 0].map(x => Math.fround(x));
          const k = a.join(',');
          let j = seen.get(k);
          if (j === undefined) { j = verts.length / 8 - first; seen.set(k, j); verts.push(...a); }
          idx.push(j);
        }
        meshes.push({ node: i, mat: matOf(m), first, nv: verts.length / 8 - first, ifirst, ni: idx.length - ifirst, ro: o.renderOrder || 0 });
      }
    }
  });
  return { nodes, meshes, mats, texs, lights, verts, idx, cam: { fov: W.cam.fov, near: W.cam.near, far: W.cam.far } };
}

(async () => {
  const b = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 1280, height: 800 } });
  p.on('pageerror', e => console.error('page error:', e.message));
  await p.goto('file://' + pagePath);
  await p.waitForTimeout(1500);
  // init both scenes in their first pose
  await p.evaluate(() => { S.game = 'gen'; show('g1'); GEN3.enter({}); });
  await p.waitForTimeout(800);
  await p.evaluate(() => { S.game = 'pc'; show('p1'); PC3.enter('sleeve'); });
  await p.waitForTimeout(800);
  const uriKey = {};
  for (const k in IMG) { if (Array.isArray(IMG[k])) IMG[k].forEach((u, i) => uriKey[u] = k + i); else uriKey[IMG[k]] = k; }
  // the page needs uriKey: pass it once
  await p.evaluate(u => { window.__uriKey = u; }, uriKey);
  for (const which of ['pc', 'gen']) {
    const d = await p.evaluate(([w, f]) => { const fn = eval('(' + f + ')'); return fn(w, window.__uriKey); }, [which, dump.toString()]);
    const dir = path.join(OUT, which === 'pc' ? 'pc94' : 'gen94');
    fs.mkdirSync(path.join(dir, 'tex'), { recursive: true });
    const L = [];
    L.push(`obscene 2`);
    L.push(`camera ${d.cam.fov} ${d.cam.near} ${d.cam.far}`);
    d.texs.forEach((t, i) => {
      const w = `${t.wrapS} ${t.wrapT} ${t.off[0]} ${t.off[1]} ${t.rep[0]} ${t.rep[1]} ${t.flipY}`;
      if (t.kind === 'art') L.push(`tex ${i} art ${t.key} ${w}`);
      else if (t.kind === 'notch') L.push(`tex ${i} notch ${t.key || '-'} ${w} ${t.w} ${t.h} ${t.nr} ${t.ny} ${t.cream}`);
      else if (t.kind === 'png') { const f = `tex/t${i}.png`; fs.writeFileSync(path.join(dir, f), Buffer.from(t.png.split(',')[1], 'base64')); L.push(`tex ${i} file ${f} ${w}`); }
      else L.push(`tex ${i} none - ${w}`);
    });
    d.mats.forEach((m, i) => L.push(`mat ${i} ${m.type} ${m.color.join(' ')} ${m.map} ${m.opacity} ${m.transparent} ${m.alphaTest} ${m.side} ${m.depthWrite} ${m.visible} ${m.spec.join(' ')} ${m.shin} ${m.emis.join(' ')} ${m.poff} ${m.vcol}`));
    const q = s => (s || '-').replace(/\s/g, '_');
    d.nodes.forEach((n, i) => L.push(`node ${i} ${n.parent} ${q(n.name)} ${q(n.id)} ${n.pos.join(' ')} ${n.rot.join(' ')} ${n.order} ${n.scale.join(' ')} ${n.vis} ${n.onX ?? 0} ${n.offX ?? 0} ${n.label ? n.label.replace(/\n/g, ' ') : ''}`));
    d.meshes.forEach((m, i) => L.push(`mesh ${i} ${m.node} ${m.mat} ${m.first} ${m.nv} ${m.ifirst} ${m.ni} ${m.ro}`));
    d.lights.forEach((l, i) => L.push(`light ${l.kind} ${l.color.join(' ')} ${l.I} ${l.ground.join(' ')} ${l.pos.join(' ')} ${l.target.join(' ')}`));
    fs.writeFileSync(path.join(dir, 'scene.txt'), L.join('\n') + '\n');
    fs.writeFileSync(path.join(dir, 'scene.vtx'), Buffer.from(new Float32Array(d.verts).buffer));
    fs.writeFileSync(path.join(dir, 'scene.idx'), Buffer.from(new Uint32Array(d.idx).buffer));
    console.log(which, 'nodes', d.nodes.length, 'meshes', d.meshes.length, 'verts', d.verts.length / 8, 'indices', d.idx.length, 'textures', d.texs.length, 'lights', d.lights.length);
  }
  await b.close();
  fs.unlinkSync(pagePath);
})();
