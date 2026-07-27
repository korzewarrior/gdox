# Website

`public` is the dependency-free source for `gdox.korze.org`.

`make site` stages a deployable tree in `../gdox-output/site/public`, adds the
canonical schemas and minisign key, and derives the stylesheet cache key from
its content. `make site-check` builds and audits the result.

Release artifacts under the production `/downloads/` path are managed
separately and must be preserved during deployment.
