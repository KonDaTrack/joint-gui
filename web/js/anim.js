// 界面动效（GSAP）。纯粹是"表现层"，**不做任何安全判断**——
// 限位、看门狗、控制权一律在下位机强制。
//
// 两条原则：
// 1) 只用在「状态变化」和「注意力引导」上，**绝不**用在实时读数本身
//    ——读数 50Hz 刷新，加动画只会让数字看不清
// 2) 全程可降级：GSAP 没加载、或系统开了"减少动态效果"，界面必须照常可用
//    （试验台多为离线环境，不能假设 CDN/脚本一定能加载）

const Anim = {
  // gsap 存在 且 用户没要求减少动效
  get ok() {
    return typeof gsap !== 'undefined' &&
           !window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  },

  /** 首次进入：三块卡片依次淡入上浮 */
  entrance() {
    if (!this.ok) return;
    gsap.from('.layout > .card', {
      opacity: 0, y: 18, duration: 0.7, ease: 'expo.out', stagger: 0.09,
    });
    gsap.from('.card-rack', { opacity: 0, y: -10, duration: 0.6, ease: 'expo.out' });
  },

  /** 切换从站：读数区做一次短促的刷新过渡（动画作用在容器上，不碰数字本身） */
  slaveSwitch() {
    if (!this.ok) return;
    gsap.fromTo('.readouts, .telemetry',
      { opacity: 0.35 },
      { opacity: 1, duration: 0.4, ease: 'expo.out' });
  },

  /** 驱动状态变为「运行使能」：圆点+文字脉冲一下，提示"现在有电了" */
  driveEnabled() {
    if (!this.ok) return;
    gsap.fromTo('#valState',
      { scale: 1 },
      { scale: 1.12, duration: 0.22, ease: 'back.out(2.2)', yoyo: true, repeat: 1 });
    gsap.fromTo('#dotState',
      { boxShadow: '0 0 0 0 rgba(49,255,176,0.8)' },
      { boxShadow: '0 0 0 10px rgba(49,255,176,0)', duration: 0.5, ease: 'power2.out' });
  },

  /** 故障出现：闪烁强调 2 次（这是最该被注意到的变化） */
  faultAppeared() {
    if (!this.ok) return;
    gsap.fromTo('#valError',
      { opacity: 1 },
      { opacity: 0.15, duration: 0.14, repeat: 5, yoyo: true,
        ease: 'power1.inOut', onComplete: () => { document.getElementById('valError').style.opacity = 1; } });
    gsap.fromTo('#valError', { scale: 1 }, { scale: 1.15, duration: 0.2, yoyo: true, repeat: 1 });
  },

  /** 切换操作模式：字段先淡出再滑入，替代原来的瞬间显隐 */
  fieldsChanged() {
    if (!this.ok) return;
    const shown = document.querySelectorAll('.field:not(.hidden)');
    gsap.from(shown, { opacity: 0, x: -10, duration: 0.5, ease: 'expo.out', stagger: 0.04 });
  },

  /** 控制权变化：顶栏徽章脉冲 */
  ownershipChanged() {
    if (!this.ok) return;
    gsap.fromTo('#ownerBadge',
      { scale: 1 },
      { scale: 1.1, duration: 0.26, yoyo: true, repeat: 1, ease: 'back.out(2.2)' });
    gsap.fromTo('#ownerBadge',
      { boxShadow: '0 0 0 0 rgba(255,183,77,0.7)' },
      { boxShadow: '0 0 0 8px rgba(255,183,77,0)', duration: 0.5, ease: 'power2.out' });
  },

  /** 开始记录波形：曲线卡片亮一下边框 */
  chartStarted() {
    if (!this.ok) return;
    const card = document.querySelector('.card-chart');
    gsap.fromTo(card,
      { borderColor: '#38E1FF' },
      { borderColor: 'rgba(90,190,235,0.16)', duration: 0.9, ease: 'power2.out' });
  },

  /** 按钮点击的轻微反馈（按下时缩一下） */
  bindButtonFeedback() {
    if (!this.ok) return;
    document.querySelectorAll('.btn').forEach((btn) => {
      btn.addEventListener('pointerdown', () => {
        if (btn.disabled) return;
        gsap.to(btn, { scale: 0.97, duration: 0.08, ease: 'power2.out' });
      });
      const up = () => gsap.to(btn, { scale: 1, duration: 0.12, ease: 'power2.out' });
      btn.addEventListener('pointerup', up);
      btn.addEventListener('pointerleave', up);
    });
  },
};
