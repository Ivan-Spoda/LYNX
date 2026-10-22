import vm, sys
import sys
SET={'rc':3,'de':2,'cla':20}[sys.argv[1]]
names=['st1k','st100','st6k','bu','bu2','pk','go','kwe','lu']
p=sys.argv[1]; states=sys.argv[2:]
vm.run(p,None,{st:['loadcomponent "%s"'%vm.sig.winpath(vm.W+'/st/%s.bin'%st)] for st in states},names,vm.W+'/dyn2',settle=SET)
print('done',p)
