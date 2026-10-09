// 页面切换 + hash 同步。纯表现层，不含任何安全判断。
//
// 三条约束（见 docs/superpowers/specs/2026-10-09-shell-design.md）：
//  1) 切页只切 hidden，**不重建 DOM**
//     —— 重建会销毁 3D canvas 的 WebGL 上下文、清空波形缓冲，且重新握手要时间
//  2) 安全条**不参与切换**，它是三个页面共用的唯一实例
//  3) 页面在 hidden 状态下所有元素尺寸为 0，箱内做尺寸自适应的东西（波形 canvas、
//     读数区二分字号）**必须重测**。所以这里提供 onChange 回调，别在页面里各写一套。
//
// hash 为空时的默认页由调用方传入（app.js）。分两阶段：
//   子系统 1 交付时 → 'monitor'（另两页还是占位壳，落过去像页面坏了）
//   子系统 4 交付后 → 'boot'

const Router = {
  PAGES: ['boot', 'config', 'monitor'],
  current: null,

  /** 页面变化回调。app.js 用它重测尺寸 / 重绘。 */
  _onChange: [],
  onChange(fn) { this._onChange.push(fn); },

  /**
   * @param {string} defaultPage 无 hash 时落到哪一页
   */
  init(defaultPage) {
    this.defaultPage = defaultPage;

    document.querySelectorAll('#nav button').forEach((btn) => {
      btn.addEventListener('click', () => this.go(btn.dataset.page));
    });
    window.addEventListener('hashchange', () => this._sync());

    this._sync();
  },

  /** 按当前 hash（或默认页）落位 */
  _sync() {
    const raw = (location.hash || '').replace(/^#/, '');
    this.show(this.PAGES.includes(raw) ? raw : this.defaultPage);
  },

  /**
   * 切到指定页。**同步落位**，然后才同步地址栏。
   *
   * 不能只写 hash 等 hashchange —— 那个事件是异步的，调用方拿到返回值时
   * 页面还没切（探针里踩过：`Router.go('config')` 之后 current 仍是 'monitor'，
   * 看起来像路由坏了）。写 hash 之后浏览器还会补一次 hashchange，
   * 那时 _sync → show(同一页)，show 内部靠 prev===name 判断不重放动画，是幂等的。
   */
  go(name) {
    if (!this.PAGES.includes(name)) return;
    this.show(name);
    if (location.hash !== '#' + name) location.hash = name;
  },

  show(name) {
    if (!this.PAGES.includes(name)) name = this.defaultPage;
    const prev = this.current;
    this.current = name;

    this.PAGES.forEach((p) => {
      const el = document.getElementById('page-' + p);
      if (el) el.hidden = (p !== name);
    });

    document.querySelectorAll('#nav button').forEach((b) => {
      const on = b.dataset.page === name;
      b.classList.toggle('active', on);
      b.setAttribute('aria-selected', String(on));
    });

    // 只有真正换页才播入场，且**不重建节点**（只加减 class）
    const el = document.getElementById('page-' + name);
    if (el && prev !== name) {
      el.classList.remove('page-enter');
      void el.offsetWidth;          // 强制重排，否则同一个 class 不会重放动画
      el.classList.add('page-enter');
    }

    // 通知外部重测尺寸。同步调用：调用方内部若要读布局，必须在本次切换后读，
    // 拖到下一帧读会读到中间态。
    this._onChange.forEach((fn) => {
      try { fn(name, prev); } catch (e) { console.error('[router] onChange 异常', e); }
    });
  },
};
