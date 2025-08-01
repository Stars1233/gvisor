// Copyright 2018 The gVisor Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "test/util/capability_util.h"
#include "test/util/file_descriptor.h"
#include "test/util/fs_util.h"
#include "test/util/save_util.h"
#include "test/util/temp_path.h"
#include "test/util/test_util.h"

namespace gvisor {
namespace testing {

namespace {

TEST(UnlinkTest, IsDir) {
  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());

  EXPECT_THAT(unlink(dir.path().c_str()), SyscallFailsWithErrno(EISDIR));
}

TEST(UnlinkTest, DirNotEmpty) {
  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());

  int fd;
  std::string path = JoinPath(dir.path(), "ExistingFile");
  EXPECT_THAT(fd = open(path.c_str(), O_RDWR | O_CREAT, 0666),
              SyscallSucceeds());
  EXPECT_THAT(close(fd), SyscallSucceeds());
  EXPECT_THAT(rmdir(dir.path().c_str()), SyscallFailsWithErrno(ENOTEMPTY));
}

TEST(UnlinkTest, Rmdir) {
  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  EXPECT_THAT(rmdir(dir.path().c_str()), SyscallSucceeds());
}

TEST(UnlinkTest, AtDir) {
  int dirfd;
  auto tmpdir = GetAbsoluteTestTmpdir();
  EXPECT_THAT(dirfd = open(tmpdir.c_str(), O_DIRECTORY, 0), SyscallSucceeds());

  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDirIn(tmpdir));
  auto dir_relpath =
      ASSERT_NO_ERRNO_AND_VALUE(GetRelativePath(tmpdir, dir.path()));
  EXPECT_THAT(unlinkat(dirfd, dir_relpath.c_str(), AT_REMOVEDIR),
              SyscallSucceeds());
  ASSERT_THAT(close(dirfd), SyscallSucceeds());
}

TEST(UnlinkTest, AtDirDegradedPermissions) {
  // Drop capabilities that allow us to override file and directory permissions.
  AutoCapability cap1(CAP_DAC_OVERRIDE, false);
  AutoCapability cap2(CAP_DAC_READ_SEARCH, false);

  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());

  int dirfd;
  ASSERT_THAT(dirfd = open(dir.path().c_str(), O_DIRECTORY, 0),
              SyscallSucceeds());

  std::string sub_dir = JoinPath(dir.path(), "NewDir");
  EXPECT_THAT(mkdir(sub_dir.c_str(), 0755), SyscallSucceeds());
  EXPECT_THAT(fchmod(dirfd, 0444), SyscallSucceeds());
  EXPECT_THAT(unlinkat(dirfd, "NewDir", AT_REMOVEDIR),
              SyscallFailsWithErrno(EACCES));
  ASSERT_THAT(close(dirfd), SyscallSucceeds());
}

// Files cannot be unlinked if the parent is not writable and executable.
TEST(UnlinkTest, ParentDegradedPermissions) {
  // Drop capabilities that allow us to override file and directory permissions.
  AutoCapability cap1(CAP_DAC_OVERRIDE, false);
  AutoCapability cap2(CAP_DAC_READ_SEARCH, false);

  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  auto file = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateFileIn(dir.path()));

  ASSERT_THAT(chmod(dir.path().c_str(), 0000), SyscallSucceeds());

  struct stat st;
  ASSERT_THAT(stat(file.path().c_str(), &st), SyscallFailsWithErrno(EACCES));
  ASSERT_THAT(unlinkat(AT_FDCWD, file.path().c_str(), 0),
              SyscallFailsWithErrno(EACCES));

  // Non-existent files also return EACCES.
  const std::string nonexist = JoinPath(dir.path(), "doesnotexist");
  ASSERT_THAT(stat(nonexist.c_str(), &st), SyscallFailsWithErrno(EACCES));
  ASSERT_THAT(unlinkat(AT_FDCWD, nonexist.c_str(), 0),
              SyscallFailsWithErrno(EACCES));
}

TEST(UnlinkTest, AtBad) {
  int dirfd;
  EXPECT_THAT(dirfd = open(GetAbsoluteTestTmpdir().c_str(), O_DIRECTORY, 0),
              SyscallSucceeds());

  // Try removing a directory as a file.
  std::string path = JoinPath(GetAbsoluteTestTmpdir(), "NewDir");
  EXPECT_THAT(mkdir(path.c_str(), 0755), SyscallSucceeds());
  EXPECT_THAT(unlinkat(dirfd, "NewDir", 0), SyscallFailsWithErrno(EISDIR));
  EXPECT_THAT(unlinkat(dirfd, "NewDir", AT_REMOVEDIR), SyscallSucceeds());

  // Try removing a file as a directory.
  int fd;
  EXPECT_THAT(fd = openat(dirfd, "UnlinkAtFile", O_RDWR | O_CREAT, 0666),
              SyscallSucceeds());
  EXPECT_THAT(unlinkat(dirfd, "UnlinkAtFile", AT_REMOVEDIR),
              SyscallFailsWithErrno(ENOTDIR));
  EXPECT_THAT(unlinkat(dirfd, "UnlinkAtFile/", 0),
              SyscallFailsWithErrno(ENOTDIR));
  ASSERT_THAT(close(fd), SyscallSucceeds());
  EXPECT_THAT(unlinkat(dirfd, "UnlinkAtFile", 0), SyscallSucceeds());

  // Cleanup.
  ASSERT_THAT(close(dirfd), SyscallSucceeds());
}

TEST(UnlinkTest, AbsTmpFile) {
  int fd;
  std::string path = JoinPath(GetAbsoluteTestTmpdir(), "ExistingFile");
  EXPECT_THAT(fd = open(path.c_str(), O_RDWR | O_CREAT, 0666),
              SyscallSucceeds());
  EXPECT_THAT(close(fd), SyscallSucceeds());
  EXPECT_THAT(unlink(path.c_str()), SyscallSucceeds());
}

TEST(UnlinkTest, TooLongName) {
  EXPECT_THAT(unlink(std::vector<char>(16384, '0').data()),
              SyscallFailsWithErrno(ENAMETOOLONG));
}

TEST(UnlinkTest, BadNamePtr) {
  EXPECT_THAT(unlink(reinterpret_cast<char*>(1)),
              SyscallFailsWithErrno(EFAULT));
}

TEST(UnlinkTest, AtFile) {
  const FileDescriptor dirfd = ASSERT_NO_ERRNO_AND_VALUE(
      Open(GetAbsoluteTestTmpdir(), O_DIRECTORY, 0666));
  int fd;
  EXPECT_THAT(fd = openat(dirfd.get(), "UnlinkAtFile", O_RDWR | O_CREAT, 0666),
              SyscallSucceeds());
  EXPECT_THAT(close(fd), SyscallSucceeds());
  EXPECT_THAT(unlinkat(dirfd.get(), "UnlinkAtFile", 0), SyscallSucceeds());
}

TEST(UnlinkTest, OpenFile) {
  // TODO(b/400287667): Enable save/restore for local gofer.
  DisableSave ds;
  if (IsRunningOnRunsc()) {
    ds.reset();
  }
  TempPath file = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateFile());
  int fd;
  EXPECT_THAT(fd = open(file.path().c_str(), O_RDWR, 0666), SyscallSucceeds());
  EXPECT_THAT(unlink(file.path().c_str()), SyscallSucceeds());
  EXPECT_THAT(close(fd), SyscallSucceeds());
}

TEST(RmdirTest, OpenDirectory) {
  // TODO(b/400287667): Enable save/restore for local gofer.
  DisableSave ds;
  if (IsRunningOnRunsc()) {
    ds.reset();
  }
  TempPath dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  int fd;
  EXPECT_THAT(fd = open(dir.path().c_str(), O_RDONLY | O_DIRECTORY),
              SyscallSucceeds());
  EXPECT_THAT(rmdir(dir.path().c_str()), SyscallSucceeds());
  EXPECT_THAT(close(fd), SyscallSucceeds());
}

TEST(UnlinkTest, CannotRemoveDots) {
  auto file = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateFile());
  const std::string self = JoinPath(file.path(), ".");
  ASSERT_THAT(unlink(self.c_str()), SyscallFailsWithErrno(ENOTDIR));
  const std::string parent = JoinPath(file.path(), "..");
  ASSERT_THAT(unlink(parent.c_str()), SyscallFailsWithErrno(ENOTDIR));
}

TEST(UnlinkTest, CannotRemoveRoot) {
  ASSERT_THAT(unlinkat(-1, "/", AT_REMOVEDIR), SyscallFailsWithErrno(EBUSY));
}

TEST(UnlinkTest, CannotRemoveRootWithAtDir) {
  const FileDescriptor dirfd = ASSERT_NO_ERRNO_AND_VALUE(
      Open(GetAbsoluteTestTmpdir(), O_DIRECTORY, 0666));
  ASSERT_THAT(unlinkat(dirfd.get(), "/", AT_REMOVEDIR),
              SyscallFailsWithErrno(EBUSY));
}

TEST(RmdirTest, CannotRemoveDots) {
  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  const std::string self = JoinPath(dir.path(), ".");
  ASSERT_THAT(rmdir(self.c_str()), SyscallFailsWithErrno(EINVAL));
  const std::string parent = JoinPath(dir.path(), "..");
  ASSERT_THAT(rmdir(parent.c_str()), SyscallFailsWithErrno(ENOTEMPTY));
}

TEST(RmdirTest, CanRemoveWithTrailingSlashes) {
  auto dir1 = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  const std::string slash = absl::StrCat(dir1.path(), "/");
  ASSERT_THAT(rmdir(slash.c_str()), SyscallSucceeds());
  auto dir2 = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  const std::string slashslash = absl::StrCat(dir2.path(), "//");
  ASSERT_THAT(rmdir(slashslash.c_str()), SyscallSucceeds());
}

TEST(UnlinkTest, UnlinkAtEmptyPath) {
  auto dir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());

  auto file = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateFileIn(dir.path()));
  auto fd = ASSERT_NO_ERRNO_AND_VALUE(Open(file.path(), O_RDWR, 0666));
  EXPECT_THAT(unlinkat(fd.get(), "", 0), SyscallFailsWithErrno(ENOENT));

  auto dirInDir = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDirIn(dir.path()));
  auto dirFD = ASSERT_NO_ERRNO_AND_VALUE(
      Open(dirInDir.path(), O_RDONLY | O_DIRECTORY, 0666));
  EXPECT_THAT(unlinkat(dirFD.get(), "", AT_REMOVEDIR),
              SyscallFailsWithErrno(ENOENT));
}

// The primary purpose of this test is to verify that save/restore works for
// open file descriptors to deleted files and directories.
TEST(UnlinkTest, UnlinkWithOpenFDs) {
  // TODO: b/400287667 - Enable save/restore for local gofer.
  DisableSave ds;
  if (IsRunningOnRunsc()) {
    ds.reset();
  }

  // Create some nested directories.
  TempPath foo = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDir());
  TempPath bar = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDirIn(foo.path()));
  TempPath baz = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateDirIn(bar.path()));

  // Create a file and directory in the inner most directory.
  const std::string file_path = JoinPath(baz.path(), "file");
  FileDescriptor file_fd =
      ASSERT_NO_ERRNO_AND_VALUE(Open(file_path, O_RDWR | O_CREAT, 0666));
  const char kHello[] = "hello";
  ASSERT_THAT(write(file_fd.get(), kHello, sizeof(kHello)),
              SyscallSucceedsWithValue(sizeof(kHello)));

  const std::string dir_path = JoinPath(baz.path(), "dir");
  ASSERT_THAT(mkdir(dir_path.c_str(), 0777), SyscallSucceeds());
  FileDescriptor dir_fd =
      ASSERT_NO_ERRNO_AND_VALUE(Open(dir_path, O_RDONLY | O_DIRECTORY));

  // Unlink "file" and "dir".
  EXPECT_THAT(unlink(file_path.c_str()), SyscallSucceeds());
  EXPECT_THAT(rmdir(dir_path.c_str()), SyscallSucceeds());

  // Recreate files in the same position. S/R should be able to handle this.
  FileDescriptor new_file_fd =
      ASSERT_NO_ERRNO_AND_VALUE(Open(file_path, O_RDWR | O_CREAT, 0666));
  const char kWorld[] = "world";
  ASSERT_THAT(write(new_file_fd.get(), kWorld, sizeof(kWorld)),
              SyscallSucceedsWithValue(sizeof(kWorld)));
  new_file_fd.reset();
  ASSERT_THAT(mkdir(dir_path.c_str(), 0777), SyscallSucceeds());

  // Unlink "file" and "dir" again.
  EXPECT_THAT(unlink(file_path.c_str()), SyscallSucceeds());
  EXPECT_THAT(rmdir(dir_path.c_str()), SyscallSucceeds());

  // Delete the remaining directories.
  EXPECT_THAT(rmdir(baz.path().c_str()), SyscallSucceeds());
  EXPECT_THAT(rmdir(bar.path().c_str()), SyscallSucceeds());
  EXPECT_THAT(rmdir(foo.path().c_str()), SyscallSucceeds());

  // Verify that the original file contents were preserved across unlink/rmdir.
  char buf[sizeof(kHello)] = {};
  ASSERT_THAT(pread(file_fd.get(), buf, sizeof(buf), 0),
              SyscallSucceedsWithValue(sizeof(buf)));
  EXPECT_STREQ(buf, kHello);
}

// The primary goal of this test is to ensure that a write-only FD to a deleted
// file is savable.
TEST(UnlinkTest, UnlinkWithOpenFDsWriteOnly) {
  // TODO(b/400287667): Enable save/restore for local gofer.
  DisableSave ds;
  if (IsRunningOnRunsc()) {
    ds.reset();
  }

  // Create a file.
  TempPath file = ASSERT_NO_ERRNO_AND_VALUE(TempPath::CreateFile());

  // Open the file with O_WRONLY.
  FileDescriptor file_fd =
      ASSERT_NO_ERRNO_AND_VALUE(Open(file.path(), O_WRONLY, 0222));

  // Unlink the file.
  EXPECT_THAT(unlink(file.path().c_str()), SyscallSucceeds());

  // Write to the file.
  const char kHello[] = "hello";
  ASSERT_THAT(write(file_fd.get(), kHello, sizeof(kHello)),
              SyscallSucceedsWithValue(sizeof(kHello)));

  // TODO(b/400287667): When running with local gofer AND cache policy = none,
  // stat-ing a deleted file returns ENOENT.
  if (IsRunningOnRunsc()) {
    // Stat the file to verify its size.
    struct stat st;
    ASSERT_THAT(fstat(file_fd.get(), &st), SyscallSucceeds());
    EXPECT_EQ(st.st_size, sizeof(kHello));
  }
}

}  // namespace

}  // namespace testing
}  // namespace gvisor
