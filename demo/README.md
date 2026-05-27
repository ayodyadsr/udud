# demo

Reproducible demo of `xcull` for the project README.

## generate the GIF

Install [vhs](https://github.com/charmbracelet/vhs) (single Go binary,
no other deps), then from the repo root:

```sh
make demo
```

Equivalent to:

```sh
vhs demo/demo.tape
```

This produces `demo/xcull.gif`. Commit it back to the repo so the README
embed renders on GitHub.

## fixture

`demo/urls.txt` is a 51-line synthetic recon dump that exercises every
class the README features list mentions: numeric IDs, UUIDs, JSESSIONID
matrix parameters, title-slug blog posts, asset noise, tracker
parameters, admin/debug endpoints, and backup files. It is deliberately
small enough that the GIF stays short.

The fixture is checked in so the demo is byte-for-byte reproducible.
