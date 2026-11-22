// Node loads this via createRequire, so dynamic imports use the normal loader.
let runtime;
module.exports = (input, entry, complete, reject) => {
  runtime ??= import("./runtime.js").then(module => module.bind(entry));
  return runtime.then(invoke => invoke(input)).then(complete, reject);
};
