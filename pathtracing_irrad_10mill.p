set terminal postscript  enhanced color eps
set   autoscale                        # scale axes automatically
unset log                              # remove any log-scaling
unset label                            # remove any previous labels
set ytic auto                          # set ytics automatically
set title "Irradiance estimate using path tracing (after 10 mill. rays)"
set xlabel "Measurement point"
set ylabel "Irradiance"

#set pm3d
#set dgrid3d 100,100,2
set grid
set yrange [0:0.8]

plot "pathtracing_irrad.dat" using 100 with lines lc rgb "black" title ""