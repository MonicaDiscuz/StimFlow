#include "app/FolderPicker.h"

#include "gfx/Hr.h"

#include <shobjidl.h>
#include <wrl/client.h>

namespace sf {

std::filesystem::path PickFolder(HWND owner, const std::filesystem::path& initial) {
  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  Check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(dialog.GetAddressOf())),
        "CoCreateInstance(FileOpenDialog)");

  DWORD options = 0;
  Check(dialog->GetOptions(&options), "IFileOpenDialog::GetOptions");
  Check(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST),
        "IFileOpenDialog::SetOptions");
  Check(dialog->SetTitle(L"选择一个图片文件夹"), "IFileOpenDialog::SetTitle");

  if (!initial.empty()) {
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                              IID_PPV_ARGS(item.GetAddressOf())))) {
      dialog->SetFolder(item.Get());
    }
  }

  const HRESULT result = dialog->Show(owner);
  if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
    return {};
  }
  Check(result, "IFileOpenDialog::Show");

  Microsoft::WRL::ComPtr<IShellItem> selection;
  Check(dialog->GetResult(selection.GetAddressOf()), "IFileOpenDialog::GetResult");

  PWSTR path = nullptr;
  Check(selection->GetDisplayName(SIGDN_FILESYSPATH, &path),
        "IShellItem::GetDisplayName");
  std::filesystem::path folder(path);
  CoTaskMemFree(path);
  return folder;
}

}  // namespace sf
