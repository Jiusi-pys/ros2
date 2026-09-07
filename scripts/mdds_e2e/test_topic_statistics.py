import unittest
from topic_statistics import oracle


class StatisticsOracle(unittest.TestCase):
    def setUp(self):
        self.received={'receive_ns':[10000000000+i*200000000 for i in range(20)],'sizes':[120]*20,'stamp_ns':8000000000}
        self.hz='average rate: 5.000\n\tmin: 0.200s max: 0.200s std dev: 0.00000s window: 8\n'
        self.bw='650 B/s from 8 messages\n\tMessage size mean: 120 B min: 120 B max: 120 B\n'
        self.delay='average delay: 4.000\n\tmin: 3.300s max: 4.700s std dev: 0.45826s window: 8\n'
    def test_valid_outputs(self):
        for verb,text in [('hz',self.hz),('bw',self.bw),('delay',self.delay)]:self.assertTrue(oracle(verb,text,self.received))
    def test_wrong_rate(self):self.assertFalse(oracle('hz',self.hz.replace('5.000','50.000'),self.received))
    def test_wrong_serialized_size(self):self.assertFalse(oracle('bw',self.bw.replace('120 B','121 B'),self.received))
    def test_zero_delay(self):self.assertFalse(oracle('delay',self.delay.replace('4.000','0.000'),self.received))
    def test_wrong_timestamp_age(self):self.assertFalse(oracle('delay',self.delay.replace('3.300','30.300').replace('4.700','40.700').replace('4.000','35.000'),self.received))
    def test_only_one_message(self):self.assertFalse(oracle('bw',self.bw.replace('8 messages','1 messages'),self.received))
    def test_missing_statistics(self):self.assertFalse(oracle('hz','no new messages',self.received))
    def test_bad_row_cannot_hide_behind_good_row(self):self.assertFalse(oracle('hz',self.hz+self.hz.replace('5.000','50.000'),self.received))


if __name__=='__main__':unittest.main()
