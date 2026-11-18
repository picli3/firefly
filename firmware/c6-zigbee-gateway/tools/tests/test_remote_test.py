import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class RemoteTestHost(unittest.TestCase):
    def test_actual_command_parser_and_persistence(self):
        idf = Path(os.environ.get('IDF_PATH', str(Path.home() / 'esp/esp-idf')))
        cjson = idf / 'components/json/cJSON'
        with tempfile.TemporaryDirectory(prefix='firefly-remote-') as tmp:
            binary = str(Path(tmp) / 'remote-test')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-variable',
                '-I' + str(ROOT / 'tools/tests/remote_stubs'), '-I' + str(ROOT / 'main'),
                '-I' + str(ROOT.parent / 'common/include'), '-I' + str(cjson),
                str(ROOT / 'tools/tests/remote_test_host.c'), str(cjson / 'cJSON.c'),
                '-lm', '-o', binary], check=True)
            subprocess.run([binary], check=True)

if __name__ == '__main__':
    unittest.main()
