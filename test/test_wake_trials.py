import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('wake',Path(__file__).resolve().parents[1]/'tools/summarize_wake_trials.py')
wake=importlib.util.module_from_spec(spec);spec.loader.exec_module(wake)
class WakeTrials(unittest.TestCase):
    def row(self):
        row=dict.fromkeys(wake.FIELDS,'')
        row.update(trial_id='one',tx_id='1',firmware_sha='abc',radio_chip='SX1262',device_node='E50470',
          device_state='resting',preamble_bytes='1024',try_index='0',frequency_hz='868950000',
          tx_start_us=str(0xFFFFFFFF-10),tx_done_us='20',rx_ready_us='30',
          other_sender_active='no',challenge_seen='no',final_response='no',response_correlated='no',
          status_received='yes',poll_completed='no',failure_reason='no_response')
        return row
    def test_unrelated_status_and_timestamp_wrap(self):
        group=wake.summarize([self.row()])[0]
        self.assertEqual(group['final_replies'],0);self.assertEqual(group['completed_polls'],0)
        self.assertEqual(group['unrelated_status'],1);self.assertEqual(group['tx_duration_us'],[31])
        self.assertEqual(group['tx_to_rx_ready_us'],[10])
    def test_rest_and_motion_are_separate(self):
        a=self.row();b=dict(a,tx_id='2',device_state='moving',preamble_bytes='48',response_correlated='yes',
                            final_response='yes',poll_completed='yes',failure_reason='none')
        groups=wake.summarize([a,b]);self.assertEqual(len(groups),2)
        self.assertEqual(sum(g['completed_polls'] for g in groups),1)
    def test_invalid_identity_and_correlation(self):
        a=self.row()
        with self.assertRaises(ValueError):wake.summarize([a,a])
        with self.assertRaises(ValueError):wake.summarize([dict(a,poll_completed='yes')])
        with self.assertRaises(ValueError):wake.summarize([dict(a,challenge_seen='yes')])
    def test_chip_limits(self):
        a=self.row()
        with self.assertRaises(ValueError):wake.summarize([dict(a,preamble_bytes='8192')])
        self.assertEqual(wake.summarize([dict(a,radio_chip='SX1276',preamble_bytes='65535')])[0]['attempts'],1)
if __name__=='__main__':unittest.main()
