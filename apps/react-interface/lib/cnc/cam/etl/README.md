# Reference output of the C++ demo

The `.ngc` files and the console summaries here are the output of `apps/quickprog` (generator
`quickprog 0.2.0`), not a self-check of the panel. The copy path tests compare the panel planner and
emitter against them word for word, so a difference in geometry or in the block order fails a test
instead of passing review.

The command lines that produced the files, from the built demo (`scripts/build-quickprog.sh`):

```
quickprog --op face --tool 1 --diameter 12 --stock 100x80x20 --top 0 --depth 5 \
  --step-down 2 --step-over 60 face.ngc
quickprog --op pocket --tool 3 --diameter 8 --x -30 --y -20 --size 60x40 --step-over 70 \
  --depth 6 --step-down 2.5 --save-qprog pocket.qprog pocket.ngc
quickprog --op circle-pocket --tool 2 --diameter 8 --pocket-dia 30 --x 10 --y 5 --depth 4 \
  --step-down 1.5 --allowance 0.3 --step-over 50 circle.ngc
quickprog --op drill --tool 4 --diameter 6 --pattern grid --x -20 --y -10 --cols 3 --rows 2 \
  --dx 20 --dy 15 --depth 12 grid.ngc
quickprog --op drill --tool 7 --diameter 5 --pattern circle --x 5 --y 5 --count 4 --radius 20 \
  --start 45 --depth 5 --drill-kind carbide ring.ngc
quickprog --op drill --tool 3 --diameter 5 --x -10 --y 5 --depth 3 hole.ngc
```

The two multi-operation files, `mixed.ngc` and `sametool.ngc`, come from a document written to a
file by the panel's own writer and read back by the demo (`--qprog`); the test builds the documents
in code (`etl-cases.ts`), one place keeps their parameters, and there is no `.qprog` file here.

`face.qprog`, `pocket.qprog`, `circle.qprog`, `grid.qprog`, `ring.qprog` and `hole.qprog` are the
documents of the same runs: add `--save-qprog <name>.qprog` to the line above and the demo writes the
document next to the `.ngc`, so both come from one command line and one set of parameters.
`steel.qprog` is a hand-written document of the format (a steel drill with an explicit R and a peck).

The parameters here are the ones of `etl-cases.ts`: the pocket follows it (`--tool 3`, ф8, `60x40`,
step-over 70), not the console defaults of an older run. A file regenerated from another line does
not match the panel and fails `pipeline.test.ts`.

`*.summary.txt` is the console text of the same run: the lines `ходов`, `длина резания` and
`Оценка времени` are the reference numbers of `path.test.ts`.
