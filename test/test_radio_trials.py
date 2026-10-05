import importlib.util, pathlib, unittest
spec=importlib.util.spec_from_file_location('trials',pathlib.Path(__file__).parents[1]/'tools/summarize_radio_trials.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Trials(unittest.TestCase):
    def row(self,trial='1',outcome='unknown'):
        return dict(trial=trial,peer='fixture-peer',rx_hz='41667',afc_hz='41667',preamble_bytes='32',outcome=outcome,crc_failures='0',overruns='0',truncations='0')
    def test_evidence_counts(self):
        g=m.summarize([self.row(),self.row('2','peer_observed_acceptance')])[0]
        self.assertEqual((g['attempts'],g['peer_acceptances'],g['authenticated_replies'],g['unknown']),(2,1,0,1))
    def test_invalid_evidence(self):
        for rows in ([self.row(),self.row()],[self.row(outcome='TX_DONE')]):
            with self.assertRaises(ValueError):m.summarize(rows)
if __name__=='__main__':unittest.main()
