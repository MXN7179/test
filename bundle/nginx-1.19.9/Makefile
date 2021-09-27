
default:	build

clean:
	rm -rf Makefile build

.PHONY:	default clean

build:
	$(MAKE) -f build/Makefile

install:
	$(MAKE) -f build/Makefile install

modules:
	$(MAKE) -f build/Makefile modules

upgrade:
	./nginx.exe -t

	kill -USR2 `cat ./logs/nginx.pid`
	sleep 1
	test -f ./logs/nginx.pid.oldbin

	kill -QUIT `cat ./logs/nginx.pid.oldbin`

.PHONY:	build install modules upgrade
