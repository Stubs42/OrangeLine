g = ""
sd = "\t\t"
for scale in range(1,13):
	g = g + sd
	nd = ""
	for note in range(0,12):
		g = g + nd + '"s' + ("00" + str(scale))[-2:] + "n" + ("00" + str(note))[-2:] + '"'
		nd = ", "
	sd = ",\n\t\t"
g = g + ",\n"
sd = "\t\t"
for scale in range(1,13):
    g = g + sd
    cd = ""
    for child in range(0,12):
        g = g + cd
        wd = ""
        for weight in range(0,12):
            g = g + wd + '"s' + ("00" + str(scale))[-2:] + "c" + ("00" + str(child))[-2:] + "w" + ("00" + str(weight))[-2:] +'"'
            wd = ", "
        cd = ",\n\t\t"
    sd = ",\n\t\t"
print g
        
