class MixedBaseConvertor:

    def __init__(self, *digit_strings):

        self._digit_strings = digit_strings

        num_digits = map(len, digit_strings)

        self._max = reduce(lambda x, y: x*y, num_digits, 1) - 1

        num_digits.reverse()
        bases = []
        total = 1
        for i in num_digits:
            bases.append(total)
            total *= i
        bases.reverse()

        self._bases = bases


    def max(self):
        """Maximum number representable in this base."""

        return self._max


    def parse(self, string):

        assert len(string) == len(self._digit_strings), string
        num = 0
        for i in range(0, len(string)):
            digit = string[i]
            base = self._bases[i]
            num += base * self._digit_strings[i].index(digit)
        return num


    def write(self, num):

        assert num <= self.max()

        word = ""
        for i in range(0, len(self._bases)):
            digit = num // self._bases[i]
            num -= digit * self._bases[i]
            word += self._digit_strings[i][digit]
        return word



class BibbleException(Exception):
    pass

vowels = "aeiou"
word_initial = "bdfghklmnpstvwz"
intervocalic = "bdfghklmnpsvwz"
word_final = "fklmnpst"

convert_CVCVC = MixedBaseConvertor(word_initial, vowels,
                                   intervocalic, vowels,
                                   word_final)
convert_VCVCV = MixedBaseConvertor(vowels, intervocalic,
                                   vowels, intervocalic,
                                   vowels)
convert_hex_quad = MixedBaseConvertor("0123456789abcdef",
                                      "0123456789abcdef",
                                      "0123456789abcdef",
                                      "0123456789abcdef")


def hex_to_bibble(hex_str):
    if len(hex_str) % 4:
        raise BibbleException, "Must have an integral number of hex quads"

    bibbles = []
    while hex_str:
        bibbles.append(quad_to_bibble(hex_str[:4]))
        hex_str = hex_str[4:]

    return "-".join(bibbles)


def quad_to_bibble(quad):
    num = convert_hex_quad.parse(quad)
    if num > convert_CVCVC.max():
        return convert_VCVCV.write(num - convert_CVCVC.max() - 1)
    else:
        return convert_CVCVC.write(num)


def bibble_to_quad(word):
    if word[0] in vowels:
        num = convert_VCVCV.parse(word)
        num += convert_CVCVC.max() + 1
    else:
        num = convert_CVCVC.parse(word)
    return convert_hex_quad.write(num)


def bibble_to_hex(bibble_str):
    words = bibble_str.split("-")
    hex_str = ""
    for word in words:
        hex_str += bibble_to_quad(word)
    return hex_str
                           

# Unit tests

import unittest

class MixedBaseConvertorTest(unittest.TestCase):

    def check(self, convertor, string, num):
        self.assertEqual(convertor.parse(string), num)
        self.assertEqual(convertor.write(num), string)

    def test_decimal(self):

        digits = "0123456789"
        decimal3 = MixedBaseConvertor(digits, digits, digits)
        
        self.assertEqual(decimal3.max(), 999)
        self.check(decimal3, "123", 123)
        self.check(decimal3, "001", 001)

    def test_hex(self):
        digits = "0123456789abcdef"
        hex5 = MixedBaseConvertor(digits, digits, digits, digits, digits)
        self.assertEqual(hex5.max(), 1048575)
        self.check(hex5, "fffff", hex5.max())
        self.check(hex5, "00f00", 3840)

    def test_mixed(self):
        decimal_digits = "0123456789"
        hex_digits = "0123456789abcdef"
        dhd = MixedBaseConvertor(decimal_digits, hex_digits, decimal_digits)
        self.assertEqual(dhd.max(), 1599)
        self.check(dhd, "9f9", dhd.max())
        self.check(dhd, "020", 20)
        self.check(dhd, "0f0", 150)
        self.check(dhd, "100", 160)
        self.check(dhd, "120", 180)
        self.check(dhd, "123", 183)


import random

class BibbleTest(unittest.TestCase):

    def roundtrip_quad(self, hex):

        self.assertEqual(hex, bibble_to_quad(quad_to_bibble(hex)))


    def roundtrip_hex_string(self, hex):

        self.assertEqual(hex, bibble_to_hex(hex_to_bibble(hex)))

    def roundtrip_bibble_word(self, word):

        self.assertEqual(word, quad_to_bibble(bibble_to_quad(word)))


    def test_bibble_words(self):

        for word in ["ababa", "zuzut", "babaf", "uvega", # extremes
                     "dodul", "lilil", "polis",          # random
                     ]:
            self.roundtrip_bibble_word(word)
        

    def test_strings(self):

        for hex_string in ["0374b0425562eca37082f3084858396d8b07693a",
                           "08bb918271a95d5da5f3540ee762c221943cd3bc",
                           "4d68147f13b6df56bda75949503020fceee154e5",
                           "398859d3fcb84a4b3927d051e7118c6ca1a0de29",
                           "9ccadb42a69cadafeead5311643d270b31d2caad",
                           "bca759249fff269df411ef45fc3fa6c7ea2ef822",
                           "be30a61e6943059d907bc3dfa2780ad1f47aa95d",
                           "c2608d2dfc7ac878d57ead8f88441f225ce1b0cf",
                           "dcc23b508d6c464af4c40b7c1c0b581850edd09b"]:
            self.roundtrip_hex_string(hex_string)


    def test_quads(self):

        for quad in ["9d3f", "b585", "62fa", "f89b", "bdb3", "6bb5", "b09c",
                     "4c8d", "008c", "9edf", "7479", "ccad", "b42a", "69ca",
                     "dafe", "ead5", "3116", "43d2", "70b3", "1d2c", "aad0"]:
            self.roundtrip_quad(quad)
        

    def test_rand_strings(self):

        for i in xrange(0, 1000):
            string = ""
            for i in range(40):
                string += random.choice("0123456789abcdef")
            self.roundtrip_hex_string(string)


    def test_rand_quads(self):

        for i in xrange(0, 1000):
            quad = ""
            for i in range(4):
                quad += random.choice("0123456789abcdef")
            self.roundtrip_quad(quad)
        
        
if __name__ == "__main__":
    unittest.main()

