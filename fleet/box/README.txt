Scripts that ran on the search box (/root/caves on cv1), kept as they were at the end of the run.
  start_g10.sh   run g10: seedgate8 (GPU phase scan and first look, CPU gate with the percolation term) and the workers.
                 ONLY_GATE=1 / ONLY_WORKERS=1 restart one side; settings come from the environment (see its header).
  start_g9.sh, retune9.sh   run g9: seedgate7 (both GPU stages, no percolation term); retune9.sh restarts its workers.
  start_g8.sh    run g8: seedgate6 (GPU phase scan, CPU table gate).
  start_g7.sh, regate7.sh   run g7: seedgate5 (CPU gate on both window sizes).
  start_g6.sh    run g6: seedgate4 (CPU gate on 1280-block windows).
  retune7_g5.sh  run g5 with the exact measure by cluster and open volume.
The box needs the sources of caves/ (gate, scanner, formula, native measurer, search3.py as search3_new.py), cubiomes in
./cubiomes with its objects cub_*.o, and CUDA (nvcc) for gpuscan.cu and gputables.cu.
