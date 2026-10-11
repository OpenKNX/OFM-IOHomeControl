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
    def test_uncorrelated_final_candidate_is_not_a_completed_request(self):
        row=dict(self.row(),final_response='yes')
        self.assertEqual(wake.summarize_requests([row])['completed_requests'],0)
    def test_console_deduplicates_dumps_and_separates_requests_from_rf_attempts(self):
        header='session_id,tx_id,try_index,device_node,ctrl0,ctrl1,command,preamble_bytes,frequency_hz,tx_start_us,tx_done_us,rx_ready_us,response_correlated,timeout,poll_completed,exchange_completed,challenge_seen,association_ambiguous,tx_done_valid,rx_ready_valid,failure_reason'
        failed='1,10,0,E50470,4B,20,03,1536,868950000,100,200,201,0,1,0,0,0,0,1,1,response_timeout'
        success='1,11,1,E50470,4B,20,03,1536,868950000,300,400,401,1,0,1,0,0,1,1,1,none'
        text='\n'.join('prefix 2WAttemptCSV '+line for line in [header,failed,header,failed,success])
        rows=wake.console_rows(text,trial_prefix='bench',firmware_sha='abc',radio_chip='SX1276',
            date='2026-10-11',device_state='resting',other_sender_active='no')
        self.assertEqual(len(rows),2);self.assertEqual(rows[1]['association_ambiguous'],'1')
        summary=wake.summarize_requests(rows)
        self.assertEqual(summary,dict(requests=1,rf_transmissions=2,completed_requests=1,
            first_attempt_completed_requests=0,retry_completed_requests=1,timeout_transmissions=1))
        with self.assertRaises(ValueError):wake.console_rows(text+'\nprefix 2WAttemptCSV '+success.replace('1,11,1','2,11,1'),
            trial_prefix='bench',firmware_sha='abc',radio_chip='SX1276',date='2026-10-11',device_state='resting',other_sender_active='no')
if __name__=='__main__':unittest.main()
