#!/usr/bin/env python
# -*- coding: utf-8 -*-

#from pylab import plotfile, show, gca, figure, xlabel, ylabel, title, legend, xlim, ylim


import numpy as np
import sys
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

name = sys.argv[1]
y_min = float(sys.argv[2])
y_max = float(sys.argv[3])


data = open('%s.mean' % name ,'r').readlines()
data_str = map(lambda x:x.strip('\n').split('\t'),data)
densities = map(lambda x: int(x[0])  , data_str[1:]  )
ttc_whith_alert = map(lambda x: float(x[1])   , data_str[1:]  )
ttc_without_alert = map(lambda x: float(x[3])   , data_str[1:]  )
ttc_whith_alert_err = map(lambda x: float(x[2])   , data_str[1:]  )
ttc_without_alert_err = map(lambda x: float(x[4])   , data_str[1:]  )

print densities
print ttc_whith_alert
print ttc_without_alert


ind = np.arange(len(ttc_whith_alert))                # the x locations for the groups
width = 0.35                      # the width of the bars


pp = PdfPages('%s.pdf' % name)

fig = plt.figure()
ax = fig.add_subplot(111)

rects1 = ax.bar(ind, ttc_without_alert, width, color='red', yerr = ttc_without_alert_err,  ecolor = 'black', capsize = 10)
rects2 = ax.bar(ind+width, ttc_whith_alert , width, color='green', yerr = ttc_whith_alert_err,  ecolor = 'black', capsize = 10)

rects1 = ax.bar(ind, ttc_without_alert, width, color='red')
rects2 = ax.bar(ind+width, ttc_whith_alert , width, color='green')

ax.set_xlim(-width,len(ind)+width)
ax.set_ylim(y_min, y_max)
ax.set_xlabel('Number of vehicles per hour')
ax.set_ylabel(name)


ax.set_xticks(ind+width)
xtickNames = ax.set_xticklabels(densities)
plt.setp(xtickNames, fontsize=10)

## add a legend
ax.legend( (rects1[0], rects2[0]), (data_str[0][3], data_str[0][1]) )

#plt.show()


	
pp.savefig()
pp.close()







