// 页面切换 + hash 同步。纯表现层，不含任何安全判断。
//
// 三条约束（见 docs/superpowers/specs/2026-10-09-shell-design.md）：
//  1) 切页只切 hidden，**不重建 DOM**
//     —— 重建会销毁 3D canvas 的 WebGL 上下文、清空波形缓冲，且重新握手要时间
//  2) 安全条**不参与切换**，它是三个页面共用的唯一实例
//  3) 页面在 hidden 状态下所有元素尺寸为 0，箱内做尺寸自适应的东西（波形 canvas、
//     读数区二分字号）**必须重测**。所以这里提供 onChange 回调，别在页面里各写一套。
//
// hash 为空时的默认页由调用方传入（app.js）。目前只剩 'monitor' 一页。
//
// ★ 本路由**只管正式界面**。开场画面（连接/握手）不是"一页"——
//   它是启动时的一次性覆盖层，连上就被摘掉，不参与路由，也不在导航里出现。

const Router = {
  PAGES: ['monitor', 'history'],
  current: null,

  // 交叉过渡最长 460ms（见 shell.css 的 page-in-*）。收尾定时器留点余量，
  // 别在动画还没跑完时就把类名清了——那会把过渡截断成一次生硬的跳变。
  _TRANS_MS: 560,
  _out: null, _inc: null, _timer: null,

  /** 页面变化回调。app.js 用它重测尺寸 / 重绘。 */
  _onChange: [],
  onChange(fn) { this._onChange.push(fn); },

  /**
   * @param {string} defaultPage 无 hash 时落到哪一页
   */
  init(defaultPage) {
    this.defaultPage = defaultPage;
    // 防呆：默认页必须自己在 PAGES 里。否则 show() 会回退到 defaultPage，
    // 而那名字同样不在 PAGES 里 → 循环里每一页都被置为 hidden，
    // **整个主界面空白**，只剩安全条。删页面时漏改这个参数就踩过。
    if (!this.PAGES.includes(defaultPage)) {
      console.error(`[router] 默认页 "${defaultPage}" 不在 PAGES 里，改用 "${this.PAGES[0]}"`);
      this.defaultPage = this.PAGES[0];
    }

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

  /** 过渡收尾：旧页隐藏、两页的动画类都清干净。
   *  单独抽出来是因为**连续快速切页**时必须有地方把上一次没收完的尾收掉，
   *  否则会留下一个永远不隐藏的页面盖在上面。 */
  _settle() {
    if (this._timer) { clearTimeout(this._timer); this._timer = null; }
    if (this._out) { this._out.hidden = true; this._out.className = 'page'; this._out = null; }
    if (this._inc) { this._inc.className = 'page'; this._inc = null; }
  },

  show(name) {
    if (!this.PAGES.includes(name)) name = this.defaultPage;
    const prev = this.current;

    document.querySelectorAll('#nav button').forEach((b) => {
      const on = b.dataset.page === name;
      b.classList.toggle('active', on);
      b.setAttribute('aria-selected', String(on));
    });

    if (name !== prev) {
      this.current = name;
      this._settle();   // 先收上一次的尾

      const inc = document.getElementById('page-' + name);
      const out = prev ? document.getElementById('page-' + prev) : null;
      const reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

      if (out && inc && !reduce) {
        // 交叉过渡：两页同时在场，方向由页面在 PAGES 里的先后决定
        const dir = this.PAGES.indexOf(name) > this.PAGES.indexOf(prev) ? 'fwd' : 'back';
        out.hidden = false;
        inc.hidden = false;
        out.className = 'page leave-' + dir;
        inc.className = 'page';        // 先摘干净…
        void inc.offsetWidth;          // …强制重排，否则同一个 class 不会重放动画

        inc.className = 'page enter-' + dir;
        this._out = out;
        this._inc = inc;
        this._timer = setTimeout(() => this._settle(), this._TRANS_MS);
      } else {
        // 首次落位 / 减少动效：直接切
        this.PAGES.forEach((p) => {
          const el = document.getElementById('page-' + p);
          if (!el) return;
          el.className = 'page';
          el.hidden = (p !== name);
        });
      }
    }

    // 通知外部重测尺寸。同步调用：调用方内部若要读布局，必须在本次切换后读，
    // 拖到下一帧读会读到中间态。
    this._onChange.forEach((fn) => {
      try { fn(name, prev); } catch (e) { console.error('[router] onChange 异常', e); }
    });
  },
};
