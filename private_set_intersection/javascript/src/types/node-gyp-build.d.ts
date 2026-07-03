// node-gyp-build ships no type declarations. It exports a single function that,
// given a package directory, resolves and loads the matching prebuilt .node
// addon (from prebuilds/<platform>-<arch>/) and returns its exports.
declare module 'node-gyp-build' {
  const load: (dir: string) => unknown
  export default load
}
