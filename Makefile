all:

symlinks:
	ln -s viewmtn/MochiKit www/MochiKit
	ln -s ../../graphs www/viewmtn/graph

usher: usher.cc
	g++ usher.cc -o usher -g -Wall `pkg-config libpqxx --cflags --libs`
