
testfunc () {
	integer a; integer b; integer t;
	a = 1071; b = 462;
	while(b != 0) {
	   t = b; b = a % b; a = t;
        }
	print(a);
}

default {
  state_entry(integer foo) {
  }
}
