# Vendored OMPEval evaluator

This directory contains the evaluator-only subset of
[zekyll/OMPEval](https://github.com/zekyll/OMPEval) at commit
`4aec210ff75b0851af0ee170b35a7899e1a4fe8f`.

Included upstream files:

- `LICENSE.txt`
- `omp/Constants.h`
- `omp/Util.h`
- `omp/Hand.h`
- `omp/HandEvaluator.h`
- `omp/HandEvaluator.cpp`
- `omp/OffsetTable.hxx`

OMPEval is ISC licensed. Its equity calculator, range parser, tests, and
libdivide dependency are not included because Felt only uses the hand evaluator.
The vendored upstream files are unmodified.
