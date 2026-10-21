import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LayoutTests(unittest.TestCase):
    def test_layouts_match_pcb(self):
        matrices = json.loads((ROOT / 'tests/matrix.json').read_text())
        for variant, count in [('1u', 60), ('2u', 58)]:
            with self.subTest(variant=variant):
                expected = [tuple(v) for v in matrices[variant]]
                self.assertEqual(len(expected), count)
                self.assertEqual(len(set(expected)), count)
                text = (ROOT / f'boards/shields/stokerjack/stokerjack_{count}.overlay').read_text()
                actual = [tuple(map(int, pair)) for pair in re.findall(r'RC\((\d+),(\d+)\)', text)]
                self.assertEqual(actual, expected)
                self.assertEqual(text.count('&key_physical_attrs'), count)
                keymap = (ROOT / f'config/stokerjack_{count}.keymap').read_text()
                layers = re.findall(r'bindings = <(.*?)>;', keymap, re.S)
                self.assertEqual(len(layers), 4)
                self.assertIn('&lt FUNC SPACE', keymap)
                self.assertIn('&lt NUM SPACE', keymap)
                self.assertIn('&lt BT ESC', keymap)
                self.assertIn('&mt LCTL ESC', keymap)
                for layer in layers:
                    self.assertEqual(layer.count('&'), count)

    def test_safe_gpio_and_flash(self):
        board = (ROOT / 'boards/te9no/stokerjack/stokerjack.dts').read_text()
        self.assertIn('reg = <0x26000 0xc6000>', board)
        self.assertIn('reg = <0xec000 0x8000>', board)
        source = (ROOT / 'src/led_control.c').read_text()
        self.assertIn('data_release()', source)
        self.assertIn('gpio_pin_configure(port1, 9, GPIO_OUTPUT_HIGH)', source)
        self.assertIn('io-channels = <&adc 6>', board)
        self.assertIn('full-ohms = <1470000>', board)


if __name__ == '__main__':
    unittest.main()
