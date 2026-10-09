// 图形能力探测。上位机**只在工控机运行**（有 GPU），WebGL 视为恒可用，
// 所以这里**不是降级链**——只是"万一拿不到，别白屏"的最小兜底。
//
// 结果放在 Gfx.caps，由 app.js 取到 state.gfx。
// （本文件先于 app.js 加载，所以不能在这里直接引用 state。）

const Gfx = {
  caps: null,

  /**
   * 探测一次并缓存。**必须主动释放 context**：
   * 浏览器同时能持有的 WebGL 上下文数量有限（约 16 个），
   * 探测用掉一个不还，后面 Three.js 再创建就可能失败——而且报错和探测看不出关系。
   */
  detect() {
    const caps = { webgl2: false, webgl: false, renderer: null, error: null };
    let canvas = null, gl = null;
    try {
      canvas = document.createElement('canvas');
      gl = canvas.getContext('webgl2');
      if (gl) caps.webgl2 = true;
      else { gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl'); }
      if (gl) {
        caps.webgl = true;
        const ext = gl.getExtension('WEBGL_debug_renderer_info');
        // 拿不到 debug 扩展时退到通用 RENDERER（大多数浏览器会把它抹成 "WebKit WebGL"）
        caps.renderer = ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL)
                            : gl.getParameter(gl.RENDERER);
      }
    } catch (e) {
      caps.error = String(e);
    } finally {
      // 归还上下文槽位
      if (gl) {
        const lose = gl.getExtension('WEBGL_lose_context');
        if (lose) lose.loseContext();
      }
      if (canvas) { canvas.width = canvas.height = 0; canvas = null; }
    }
    this.caps = caps;
    return caps;
  },

  /** 3D 是否可用 */
  get ok() { return !!(this.caps && this.caps.webgl); },

  /**
   * 不支持时在容器里显示一行提示。**不抛异常、不中断后续脚本**——
   * 页面其余部分（读数、波形、操作）必须照常工作。
   */
  showWarning(el, msg) {
    if (!el) return;
    el.innerHTML = '';
    const d = document.createElement('div');
    d.className = 'gfx-warn';
    d.textContent = msg || '此环境不支持 WebGL，三维视图不可用。其余功能不受影响。';
    el.appendChild(d);
  },
};

Gfx.detect();
