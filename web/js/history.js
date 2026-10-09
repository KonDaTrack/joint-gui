// 历史运行记录（IndexedDB）。**本地持久** —— 刷新、关浏览器、重启都还在，
// 换台机器就没了（这是工控机本地记录，不是共享库；要带走用导出）。
//
// 为什么用 IndexedDB 而不是 localStorage：
//   · 一次运行约 9000 个数（3 条轨迹 × 3000 点）。存 60 次约 4~7MB。
//   · localStorage 只有 5~10MB 且**同步**，写一次会卡住整个界面（50Hz 刷新下很明显）。
//   · IndexedDB 是异步的，且能直接存结构化数据（数组不用自己序列化）。
//
// 记录以**名称**为主键，重名覆盖（用户定的）。默认名称是保存时间。

const History = (() => {
  const DB_NAME = 'joint-runs';
  const STORE = 'runs';
  const VERSION = 1;
  const MAX_RUNS = 60;          // 上限：超了删最旧的，避免无限长大

  let dbPromise = null;

  function open() {
    if (dbPromise) return dbPromise;
    dbPromise = new Promise((resolve, reject) => {
      let req;
      try { req = indexedDB.open(DB_NAME, VERSION); }
      catch (e) { reject(e); return; }
      req.onupgradeneeded = () => {
        const db = req.result;
        if (!db.objectStoreNames.contains(STORE)) {
          const os = db.createObjectStore(STORE, { keyPath: 'name' });
          os.createIndex('ts', 'ts');
        }
      };
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    }).catch((e) => {
      // 无痕模式 / 存储被禁用时 open 会失败。**不能让它把整个页面带崩** ——
      // 返回 null，上层当"没有历史功能"处理。
      console.warn('[history] IndexedDB 不可用，历史功能关闭：', e);
      return null;
    });
    return dbPromise;
  }

  function tx(db, mode) {
    return db.transaction(STORE, mode).objectStore(STORE);
  }

  /** 默认名称 = 保存时间，精确到秒（同一秒内连发两次才会撞，撞了就覆盖） */
  function defaultName(d = new Date()) {
    const p = (n) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} `
         + `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
  }

  return {
    defaultName,

    /** 是否可用（无痕模式等情况下会不可用） */
    async ok() { return !!(await open()); },

    /**
     * 存一条运行。同名覆盖。
     * @param {object} rec { name, ts, slave, shortName, mode, target, loadNm,
     *                       ratedTorqueNm, dtMs, traces:{key:[...]}, result:{} }
     */
    async save(rec) {
      const db = await open();
      if (!db) return null;
      // 存之前先砍到上限
      await this.trim(MAX_RUNS - 1);
      return new Promise((resolve) => {
        const r = tx(db, 'readwrite').put(rec);
        r.onsuccess = () => resolve(rec);
        r.onerror = () => { console.warn('[history] 写入失败', r.error); resolve(null); };
      });
    },

    /** 全部记录，按时间**倒序**（新的在前） */
    async list() {
      const db = await open();
      if (!db) return [];
      return new Promise((resolve) => {
        const out = [];
        const r = tx(db, 'readonly').openCursor();
        r.onsuccess = () => {
          const c = r.result;
          if (c) { out.push(c.value); c.continue(); }
          else resolve(out.sort((a, b) => b.ts - a.ts));
        };
        r.onerror = () => resolve([]);
      });
    },

    async remove(name) {
      const db = await open();
      if (!db) return;
      return new Promise((resolve) => {
        const r = tx(db, 'readwrite').delete(name);
        r.onsuccess = r.onerror = () => resolve();
      });
    },

    async clear() {
      const db = await open();
      if (!db) return;
      return new Promise((resolve) => {
        const r = tx(db, 'readwrite').clear();
        r.onsuccess = r.onerror = () => resolve();
      });
    },

    /** 只保留最新的 keep 条，其余删掉 */
    async trim(keep) {
      const all = await this.list();
      if (all.length <= keep) return;
      for (const r of all.slice(keep)) await this.remove(r.name);
    },
  };
})();
