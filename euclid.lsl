
testfunc () {
	 euclid(1071, 462);
}

euclid (integer a, integer b) {
	integer t;
	while(b != 0) {
	   t = b; b = a % b; a = t;
        }
	print(a);
}

default {
  state_entry(integer foo) {
  }
}
