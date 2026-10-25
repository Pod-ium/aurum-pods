# Changelog

Pods of aurum-pods (emulators, and PicoPods Web). Until the split of the PicoPods tree their
history was kept in Pod-ium/PicoPods; the libraries' changes are in the
CHANGELOG of hydrogenium-pods.

## Unreleased

### Repository
- Moved here from Pod-ium/PicoPods, with the pods' scripted host tests. Built
  on the libraries of hydrogenium-pods (`picopods_import.cmake`).
- CI (G-host, G-fw, G-web) on pushes and pull requests; release workflow on
  tags `v*` and published releases.

### Pods


### PicoPods Web
- The site moved here (`web/`, `tools/build_web.sh`, `pages.yml`). It now
  builds every repository's pods with their own `tools/build_web_pods.sh`
  (hydrogenium-, carbo-, helium- and aurum-pods) and is also attached to
  each release as `picopods-web_<version>.zip`.
- First added in PicoPods (Unreleased): the pods in a browser, with a
  touch PicoCalc keyboard, sound, an IndexedDB SD card and pod files that
  load like cartridges.

## PicoPods 1.0.0 (2026-10-24)

- **PodBoy 1.1** (was PicoBoy 1.1): `pods/podboy`, `podboy.uf2`. Rename only.
