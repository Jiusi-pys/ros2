import unittest
from bag_transform import check_records


class TransformRecords(unittest.TestCase):
    def setUp(self):
        self.types=[('/fixture/main','std_msgs/msg/String','cdr'),('/fixture/noise','std_msgs/msg/String','cdr')]
        self.rows=[('/fixture/main',100,'one'),('/fixture/noise',101,'noise'),('/fixture/main',102,'two')]
        self.selected=[self.rows[0],self.rows[2]]
    def test_conversion_preserves_selected_samples(self):check_records(self.types,self.rows,self.types[:1],self.selected,'/fixture/main')
    def test_reindex_preserves_every_sample(self):check_records(self.types,self.rows,self.types,self.rows,None)
    def test_rejects_retimed_samples(self):
        with self.assertRaises(ValueError):check_records(self.types,self.rows,self.types[:1],[('/fixture/main',999,'one'),self.rows[2]],'/fixture/main')
    def test_rejects_noise_in_filtered_conversion(self):
        with self.assertRaises(ValueError):check_records(self.types,self.rows,self.types,self.rows,'/fixture/main')
    def test_rejects_wrong_payload_with_same_count(self):
        with self.assertRaises(ValueError):check_records(self.types,self.rows,self.types[:1],[('/fixture/main',100,'wrong'),self.rows[2]],'/fixture/main')
    def test_rejects_type_change(self):
        with self.assertRaises(ValueError):check_records(self.types,self.rows,[('/fixture/main','std_msgs/msg/Int32','cdr')],self.selected,'/fixture/main')


if __name__=='__main__':unittest.main()
