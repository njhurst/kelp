ALL:
	gcc -o benchspread -O3 -mavx2 benchspread.c
	gcc -shared -o libblockaio.so -fPIC blockaio.c rs.c rs_avx2.c -O3 -mavx2 -laio
	gcc -o rs -g rs.c rs_avx2.c test-rs.c -O3 -mavx2
	gcc -o benchavx2gf -g benchavx2gf.c rs_avx2.c rs.c -mavx2 -O3
	gcc -o test-rs -g rs.c rs_avx2.c test-rs.c -O3 -mavx2

	g++ -o test-blockaio -O3 -mavx2 test-blockaio.cpp -laio blockaio.cpp -lgtest -lgtest_main -pthread -laio
venv:
	python -m venv venv
	. venv/bin/activate && pip install -r requirements.txt

clean:
	rm -f benchspread libbenchaio.so rs benchavx2gf test-rs
	rm -rf venv

.PHONY: ALL clean venv

# vim: set noexpandtab:
