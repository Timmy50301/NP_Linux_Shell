npshell:
	g++ shell.cpp -o shell

.PHONY: clean
clean:
	rm -rf shell
