# Reference output of the C++ demo

The `.ngc` files and the console summaries here are the output of `apps/quickprog` (generator
`quickprog 0.2.0`), not a self-check of the panel. The copy path tests compare the panel planner and
emitter against them word for word, so a difference in geometry or in the block order fails a test
instead of passing review.

The command lines that produced the files, from the built demo (`scripts/build-quickprog.sh`):

```
quickprog --op face --tool 1 --diameter 12 --stock 100x80x20 --top 0 --depth 5 \
  --step-down 2 --step-over 60 face.ngc
quickprog --op pocket --tool 4 --diameter 10 --size 50x30 --x -25 --y -15 --depth 3 \
  --step-down 1.5 --allowance 0.5 --step-over 65 pocket.ngc
quickprog --op circle-pocket --tool 2 --diameter 8 --pocket-dia 30 --x 10 --y 5 --depth 4 \
  --step-down 1.5 --allowance 0.3 --step-over 50 circle.ngc
quickprog --op drill --tool 4 --diameter 6 --pattern grid --x -20 --y -10 --cols 3 --rows 2 \
  --dx 20 --dy 15 --depth 12 grid.ngc
quickprog --op drill --tool 7 --diameter 5 --pattern circle --x 5 --y 5 --count 4 --radius 20 \
  --start 45 --depth 5 --drill-kind carbide ring.ngc
quickprog --op drill --tool 3 --diameter 5 --x -10 --y 5 --depth 3 hole.ngc
quickprog --qprog mixed.qprog mixed.ngc      # three operations, G55, G83 peck
quickprog --qprog sametool.qprog sametool.ngc  # one tool twice, another H row, no M1
```

`mixed.qprog` and `sametool.qprog` are the documents of the tests, built by hand in the demo format
(see `emit.test.ts`); the documents themselves live in the test, not here, so one place keeps their
parameters.

`*.summary.txt` is the console text of the same run: the lines `ходов`, `длина резания` and
`Оценка времени` are the reference numbers of `path.test.ts`.
