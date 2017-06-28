def make_trace():
    info_f = open("./bcov.info", 'r')
    trace_f = open("./trace", "r")
    
    info = dict()
    while True:
        fline = info_f.readline()
        if not fline: break;
        line, col, expr = fline.split()
        info[line]=expr

    trace = dict()
    while True:
      fline = trace_f.readline()
      if not fline: break
      addr, line, col, cond = fline.split()
      if int(cond) == 1:
          if line in trace:
              trace[line]['true'] += 1
          else:
              trace[line] = {'true': 1, 'false': 0}
      else:
           if fline in trace:
              trace[line]['false'] += 1
           else:
              trace[line] = {'true': 0, 'false': 1}
      trace[line]['expr']=info[line]
      trace[line]['col']=col

    for el in trace:
        print("{}.{} -> {}, {} :{}".format(el, str(trace[el]['col']), str(trace[el]['true']), str(trace[el]['false']), trace[el]['expr']))

make_trace()
