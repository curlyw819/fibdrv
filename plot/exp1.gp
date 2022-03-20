reset
set ylabel 'time(ns)'
set title 'fib alogo cmp'
set key left top
set term png enhanced font 'Verdana,10'
set output 'exp1.png'
plot [2:500][:] \
'exp1.out' using 1:2 with linespoints linewidth 2 title "transporting time between kernel and user space",\
'' using 1:3 with linespoints linewidth 2 title "executing time in user space",\
'' using 1:4 with linespoints linewidth 2 title "executing time in kernel",\
