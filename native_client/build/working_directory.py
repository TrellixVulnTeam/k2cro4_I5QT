#!/usr/bin/python
# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Classes to create working directories of various sorts."""

import logging
import os
import shutil
import tempfile


class FixedWorkingDirectory(object):
  """Working directory manager to be used with 'with'.

  The manager cleanups up the directory initially if it's
  there, but leaves it in place at the end.
  Done in this style for drop in compatibility with TemporaryWorkingDirectory
  below.

  Example uses:
    for WorkingDirectory('mydir') as work_dir:
      ...do stuff in work_dir...
  """

  def __init__(self, work_dir=None):
    """ Constructor.

    Args:
      work_dir: A selected working directory.
    """
    self._work_dir = os.path.abspath(work_dir)

  def __enter__(self):
    if os.path.exists(self._work_dir):
      logging.debug('Removing %s...' % self._work_dir)
      shutil.rmtree(self._work_dir)
    logging.debug('Creating %s...' % self._work_dir)
    os.mkdir(self._work_dir)
    return self._work_dir

  def __exit__(self, _type, _value, _trackback):
    pass


class TemporaryWorkingDirectory(object):
  """Working directory manager to be used with 'with'.

  The manager creates a temporary working directory and cleans it
  up at the end.

  Example uses:
    for TemporaryWorkingDirectory() as work_dir:
      ...do stuff in work_dir...
  """
  def __enter__(self):
    self._work_dir = tempfile.mkdtemp(prefix='workdir', suffix='.tmp')
    return self._work_dir

  def __exit__(self, _type, _value, _trackback):
    shutil.rmtree(self._work_dir)
