// 三维关节视图（Three.js）。**ES 模块** —— three 是 ESM 包，必须走 import map，
// 而 import map 只有模块脚本能用。它是 deferred 的，会在 app.js 之后执行，
// 所以：自己负责初始化，只对外暴露 resize()/setActive() 供 app.js 调用
// （app.js 里一律用 `window.Joint3D?.xxx()`，别假设它已经在了）。
//
// 模型是**程序化搭的**，不加载任何模型文件 —— 试验台多为离线环境。
//   固定件：壳体筒 + 后端盖 + 刚轮齿圈
//   输出件：柔轮（波浪环）+ 输出螺栓圈 + 键槽   ← 跟随 positionDeg
//   输入件：波发生器（椭圆环）                  ← 跟随 positionDeg × 6
//
// 上位机只在工控机跑，WebGL 视为恒可用；拿不到时显示一行提示，不白屏、不中断其它脚本。

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

const Joint3D = (() => {
  const canvas = document.getElementById('joint3d');
  if (!canvas) return null;

  // ---- 能力兜底：拿不到 WebGL 就显示提示，不再往下走 ----
  // 注意用裸名 Gfx 而不是 window.Gfx —— 它是 classic 脚本顶层的 `const`，
  // 落在**全局词法环境**里，不挂在 window 上。模块能按名字看到它。
  // 用 typeof 判存在，未定义时不会抛。
  const hasGfx = (typeof Gfx !== 'undefined') && Gfx.ok;
  if (!hasGfx) {
    const box = canvas.parentElement;
    box.innerHTML = '';
    const d = document.createElement('div');
    d.className = 'gfx-warn';
    d.textContent = '此环境不支持 WebGL，三维视图不可用。其余功能不受影响。';
    box.appendChild(d);
    return null;
  }

  const RATIO = 6;          // 波发生器视觉转速倍率（真实 101:1 会糊成一片）
  const d2r = Math.PI / 180;

  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
  } catch (e) {
    return null;            // 交给上面的 Gfx 提示；这里不抛，免得中断后续脚本
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.0;

  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;

  const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 100);
  camera.position.set(2.35, 1.75, 4.30);

  const controls = new OrbitControls(camera, canvas);
  controls.target.set(0, 0, -0.05);
  controls.enableDamping = true;
  controls.dampingFactor = 0.075;
  controls.enablePan = false;
  controls.minDistance = 2.6;
  controls.maxDistance = 9;
  controls.minPolarAngle = 0.25;
  controls.maxPolarAngle = Math.PI - 0.25;
  // 滚轮缩放放慢一档：默认速度在大屏幕上"一跳一跳"的
  controls.zoomSpeed = 0.62;
  controls.rotateSpeed = 0.85;

  // ---- 平滑缩放 ----
  // 不用 controls 自带的离散缩放，而是**逐帧把相机距离插值逼近目标值**，
  // 所以按一下按钮 / 点一下滚轮，镜头是"滑"过去的，不是跳过去的。
  let zoomTarget = null;           // 目标距离，null = 没有正在进行的缩放
  const ZOOM_STEP = 0.78;          // 每按一次乘这个系数（<1 = 拉近）
  const ZOOM_LERP = 0.16;          // 每帧逼近目标的比例

  function curDist() { return camera.position.distanceTo(controls.target); }
  function zoomBy(f) {
    const base = zoomTarget != null ? zoomTarget : curDist();
    zoomTarget = Math.min(controls.maxDistance, Math.max(controls.minDistance, base * f));
  }
  function resetView() {
    // 复位也走插值：把距离和朝向都设成目标，由下面每帧逼近
    camera.position.set(2.35, 1.75, 4.30);
    controls.target.set(0, 0, -0.05);
    zoomTarget = null;
  }

  /**
   * 进出全屏时调用。浏览器切全屏是**瞬时**的，画布尺寸一跳、模型在屏幕上的
   * 占比跟着跳 —— 光靠淡入盖不住"东西突然变大/变小"这个感知。
   * 这里让镜头**先回拉、再归位**（约 0.5s），把这一跳变成一个有意做的镜头动作。
   */
  function settle() {
    const d = zoomTarget != null ? zoomTarget : curDist();
    zoomTarget = Math.min(controls.maxDistance, d * 1.3);   // 先退出去
    setTimeout(() => { zoomTarget = d; }, 150);             // 再回到原位
  }

  // 环境贴图给金属底子，三盏方向光给轮廓
  scene.add(new THREE.HemisphereLight(0xcfe0ff, 0x1A1D22, 0.85));
  const key  = new THREE.DirectionalLight(0xffffff, 3.2); key.position.set(2.6, 3.2, 2.4);
  const rim  = new THREE.DirectionalLight(0x86bcff, 2.4); rim.position.set(-2.6, 0.6, -2.4);
  const warm = new THREE.DirectionalLight(0xffd2a0, 0.55); warm.position.set(-2.2, -1.6, 1.6);
  scene.add(key, rim, warm);

  // ★ 金属度不能给满（0.9 以上几乎没有漫反射，全靠环境贴图，
  //   而 RoomEnvironment 偏暗 → 壳体糊成一团黑）。0.5 左右才有金属感。
  const M = {
    housing: new THREE.MeshStandardMaterial({ color: 0x606874, metalness: 0.52, roughness: 0.40, envMapIntensity: 1.2 }),
    ring:    new THREE.MeshStandardMaterial({ color: 0x7C8492, metalness: 0.62, roughness: 0.30, envMapIntensity: 1.3 }),
    steel:   new THREE.MeshStandardMaterial({ color: 0xB6BECC, metalness: 0.78, roughness: 0.26, envMapIntensity: 1.4 }),
    dark:    new THREE.MeshStandardMaterial({ color: 0x23262C, metalness: 0.45, roughness: 0.60 }),
    flex:    new THREE.MeshStandardMaterial({ color: 0x78D6EE, metalness: 0.45, roughness: 0.22,
               emissive: 0x2E8FA8, emissiveIntensity: 0.75 }),
    wave:    new THREE.MeshStandardMaterial({ color: 0xD9A441, metalness: 0.65, roughness: 0.28,
               emissive: 0x8A5C10, emissiveIntensity: 0.65 }),
  };

  const root = new THREE.Group();
  root.rotation.x = 0.34;
  root.rotation.y = -0.46;
  scene.add(root);

  // 壳体筒（开口圆柱，前后通透，能看进内部）
  const housing = new THREE.Mesh(new THREE.CylinderGeometry(1.10, 1.10, 1.05, 128, 1, true), M.housing);
  housing.rotation.x = Math.PI / 2; housing.position.z = -0.12;
  root.add(housing);
  // 环形机加工槽（放射状加强筋在暗面板上看着像扎出来的毛刺，换成细环）
  [-0.44, -0.12, 0.20].forEach((z) => {
    const g = new THREE.Mesh(new THREE.TorusGeometry(1.105, 0.024, 8, 160), M.dark);
    g.position.z = z; root.add(g);
  });
  const bezel = new THREE.Mesh(new THREE.TorusGeometry(1.10, 0.055, 14, 160), M.ring);
  bezel.position.z = 0.42; root.add(bezel);

  // 后端盖
  const back = new THREE.Mesh(new THREE.CylinderGeometry(1.10, 1.10, 0.06, 128), M.housing);
  back.rotation.x = Math.PI / 2; back.position.z = -0.63;
  root.add(back);

  // 刚轮齿圈（固定，44 齿向内）
  const RING_R = 1.02, TOOTH_LEN = 0.13;
  for (let i = 0; i < 44; i++) {
    const a = i / 44 * Math.PI * 2;
    const t = new THREE.Mesh(new THREE.BoxGeometry(0.075, TOOTH_LEN, 0.17), M.ring);
    t.position.set(Math.cos(a) * (RING_R - TOOTH_LEN / 2), Math.sin(a) * (RING_R - TOOTH_LEN / 2), 0.14);
    t.rotation.z = a - Math.PI / 2;
    root.add(t);
  }
  const ringBand = new THREE.Mesh(new THREE.TorusGeometry(RING_R + 0.02, 0.055, 12, 140), M.ring);
  ringBand.position.z = 0.14; root.add(ringBand);

  // 输出组：柔轮 + 螺栓圈 + 键槽
  const outGroup = new THREE.Group();
  root.add(outGroup);
  (() => {
    const R = 0.80, AMP = 0.055, N = 200, pts = [];
    for (let i = 0; i < N; i++) {
      const th = i / N * Math.PI * 2;
      const r = R + AMP * Math.cos(2 * th);
      pts.push(new THREE.Vector3(Math.cos(th) * r, Math.sin(th) * r, 0));
    }
    const g = new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts, true), 300, 0.052, 12, true);
    const m = new THREE.Mesh(g, M.flex);
    m.position.z = 0.14;
    outGroup.add(m);
  })();
  for (let i = 0; i < 6; i++) {
    const a = i / 6 * Math.PI * 2;
    const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.048, 0.048, 0.10, 20), M.steel);
    bolt.rotation.x = Math.PI / 2;
    bolt.position.set(Math.cos(a) * 0.60, Math.sin(a) * 0.60, 0.30);
    outGroup.add(bolt);
  }
  const keyway = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.05, 0.05), M.flex);
  keyway.position.set(0, 0.60, 0.30);
  outGroup.add(keyway);

  // 输入组：波发生器（椭圆环 + 中心毂）
  const waveGroup = new THREE.Group();
  waveGroup.position.z = 0.14;
  root.add(waveGroup);
  (() => {
    const t = new THREE.Mesh(new THREE.TorusGeometry(0.66, 0.055, 14, 160), M.wave);
    t.scale.set(1.13, 0.90, 1);
    waveGroup.add(t);
    const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.12, 0.16, 32), M.wave);
    hub.rotation.x = Math.PI / 2;
    waveGroup.add(hub);
  })();

  // ---- 尺寸 ----
  function resize() {
    const r = canvas.getBoundingClientRect();
    // ★ 尺寸为 0 时直接返回：页面隐藏时 ResizeObserver 会带着 0 触发，
    //   照写会把画布清零；而重新显示时它不一定再触发（同 chart.js 的坑）。
    if (!r.width || !r.height) return;
    renderer.setSize(r.width, r.height, false);
    camera.aspect = r.width / r.height;
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(canvas);
  resize();

  // ---- 渲染循环 ----
  let raf = null;
  let pos = 0, vel = 0, enabled = false, running = false;

  function frame() {
    raf = requestAnimationFrame(frame);

    // 平滑缩放：先把相机沿"指向目标"的射线插值到目标距离，再交给 controls。
    // 顺序不能反 —— controls.update() 会按当前相机位置重算球坐标，
    // 先动相机它才认得。
    if (zoomTarget != null) {
      const dir = camera.position.clone().sub(controls.target);
      const cur = dir.length();
      const next = cur + (zoomTarget - cur) * ZOOM_LERP;
      if (Math.abs(next - zoomTarget) < 0.006) { zoomTarget = null; }
      camera.position.copy(controls.target).add(dir.setLength(next));
    }

    outGroup.rotation.z = pos * d2r;
    // 只在该轴**真的使能**时才转 —— 不使能却在转是假的。
    if (enabled) waveGroup.rotation.z = pos * RATIO * d2r;
    controls.update();
    renderer.render(scene, camera);
  }

  function start() { if (!raf) frame(); }
  function stop() { if (raf) { cancelAnimationFrame(raf); raf = null; } }

  // 标签页切走时停渲染 —— 没人看还烧 GPU 没道理
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) stop(); else if (running) start();
  });

  /**
   * 每次遥测调用（app.js 里 50Hz，但这里只存值，渲染由 rAF 驱动 —— 别每帧重建）
   * @param {object} t 该轴的遥测
   */
  function update(t) {
    if (!t) return;
    pos = t.positionDeg || 0;
    vel = t.velocityDps || 0;
    enabled = t.driveState === 4 && !!t.connected;
  }

  function setActive(on) {
    running = on;
    if (on) start(); else stop();
  }

  // 先跑起来（监控页是唯一页面，默认可见）
  setActive(true);

  return { resize, setActive, update, zoomBy, resetView, settle,
           get enabled() { return enabled; } };
})();

// 暴露给 app.js（它是 classic script，先于本模块执行，所以只能"事后"取用）
window.Joint3D = Joint3D;
