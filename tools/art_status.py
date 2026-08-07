"""The manifest record status vocabulary, defined once for every module that reads it.

Description: fetch_art.py writes CANDIDATE or METRIC_REJECTED; art_review.py
    reads CANDIDATE, then writes ACCEPTED or REJECTED. Metric and human
    rejections are kept apart on purpose: METRIC_REJECTED is the only corpus
    that will ever let the metric gate's guessed thresholds (see
    art_metrics.THRESHOLDS) be tuned against real photographs, and collapsing
    it into REJECTED after one review pass would destroy that data.
Author: suinevere
Dependencies: N/A
Globals: CANDIDATE, ACCEPTED, REJECTED, METRIC_REJECTED
"""

CANDIDATE = "candidate"
ACCEPTED = "accepted"
REJECTED = "rejected"
METRIC_REJECTED = "metric_rejected"
