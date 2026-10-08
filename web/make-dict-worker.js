// Runs the dictionary tools in Pyodide, off the page's main thread (web/make-dict.html).
importScripts("pyodide/pyodide.js");
const base = new URL(".", self.location).href;
let py = null;

const post = (type, data) => self.postMessage({ type, ...data });
async function bytes(path) {
  const r = await fetch(base + path);
  if (!r.ok) throw new Error(path + ": " + r.status);
  return new Uint8Array(await r.arrayBuffer());
}

async function init() {
  const list = await (await fetch(base + "make-dict-files.json")).json();
  post("log", { text: "Python を読み込んでいます" });
  py = await loadPyodide({ indexURL: base + "pyodide/" });
  const out = (text) => post("log", { text });   // the tools' own messages, e.g. "349999 keys"
  py.setStdout({ batched: out });
  py.setStderr({ batched: out });
  await py.loadPackage(list.pyodidePackages, { messageCallback: () => {} });
  await py.loadPackage(list.wheels.map((w) => base + "wheels/" + w), { messageCallback: () => {} });
  for (const dir of ["/tools", "/third_party/ejdict/src", "/third_party/wnja"]) py.FS.mkdirTree(dir);
  for (const f of list.tools) py.FS.writeFile("/tools/" + f, await bytes("tools/" + f));
  for (const f of list.ejdict) py.FS.writeFile("/third_party/ejdict/src/" + f, await bytes("ejdict/" + f));
  for (const f of list.licences) py.FS.writeFile("/third_party/wnja/" + f, await bytes("licences/" + f));
  py.FS.writeFile("/tools/make_dict.py", await bytes("make_dict.py"));
  py.runPython("import sys; sys.path.insert(0, '/tools'); import make_dict");
}

self.onmessage = async (e) => {
  try {
    if (e.data.type === "build") {
      if (!py) await init();
      const md = py.pyimport("make_dict");
      for (const [kind, buf] of Object.entries(e.data.files)) md.put_input(kind, new Uint8Array(buf));
      const names = md.step_names().toJs();
      for (let i = 0; i < names.length; i++) {
        post("step", { index: i, total: names.length, text: names[i] });
        await new Promise((r) => setTimeout(r, 0));
        md.run_step(i);
      }
      const out = [];
      for (const n of md.outputs().toJs()) {
        const b = py.FS.readFile("/work/dict/" + n);
        out.push({ name: n, buf: b.buffer });
      }
      self.postMessage({ type: "done", files: out }, out.map((f) => f.buf));
    } else if (e.data.type === "zip") {
      const z = py.pyimport("make_dict").make_zip().toJs();
      self.postMessage({ type: "zip", buf: z.buffer }, [z.buffer]);
    }
  } catch (err) {
    post("error", { text: String(err && err.message || err) });
  }
};
