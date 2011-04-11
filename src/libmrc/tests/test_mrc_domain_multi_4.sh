#! /bin/sh

set -e

openmpirun -n 4 ./test_mrc_domain_multi --npx 1 --npy 4 --npz 4 --mrc_io_type xdmf2_collective --mrc_io_nr_writers 4 --mrc_io_romio_cb_write enable

TEST=3
for a in reference_results/$TEST/*.xdmf; do 
    b=`basename $a`
    diff -u $a $b
done

for a in reference_results/$TEST/*.h5.dump; do 
    b=`basename $a .dump`
    h5dump $b > $b.dump
    diff -u $a $b.dump
    rm $b.dump
done
