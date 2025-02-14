import sys
import ctypes
from ctypes import (
    c_bool,
    c_int,
    c_char_p,
    c_ulonglong,
    byref,
    POINTER,
    string_at
)
import tkinter as tk
from tkinter import messagebox, font as tkfont, filedialog

########################################################################
#                           Часть с DLL                                #
########################################################################

# 1) Загрузка библиотеки (путь обновите под себя при необходимости)
_lib = ctypes.CDLL("C:\\Users\\User\\course_work\\dext2.dll")

# 2) Определяем собственное исключение для ошибок
class InternalDext2Exception(Exception):
    """
    Исключение, если функции из DLL вернули ошибку.
    """
    pass

# 3) Привязка типов для функций

# bool wListDisks(char*** disks, int** disksNumbers, int* size)
_lib.wListDisks.argtypes = [
    POINTER(POINTER(c_char_p)),
    POINTER(POINTER(c_int)),
    POINTER(c_int)
]
_lib.wListDisks.restype = c_bool

# void wFreeDisks(char** disks, int* disksNumbers, int size)
_lib.wFreeDisks.argtypes = [
    POINTER(c_char_p),
    POINTER(c_int),
    c_int
]
_lib.wFreeDisks.restype = None

# bool wInitHandle(int diskNum)
_lib.wInitHandle.argtypes = [c_int]
_lib.wInitHandle.restype = c_bool

# bool wListPartitions(unsigned long long** offsets, unsigned long long** partitionsLengths, int* size)
_lib.wListPartitions.argtypes = [
    POINTER(POINTER(c_ulonglong)),
    POINTER(POINTER(c_ulonglong)),
    POINTER(c_int)
]
_lib.wListPartitions.restype = c_bool

# void wFreePartitions(unsigned long long* offsets, unsigned long long* partitionsLengths, int size)
_lib.wFreePartitions.argtypes = [
    POINTER(c_ulonglong),
    POINTER(c_ulonglong),
    c_int
]
_lib.wFreePartitions.restype = None

# void wInitPartition(unsigned long long partitionStart)
_lib.wInitPartition.argtypes = [c_ulonglong]
_lib.wInitPartition.restype = None

# bool wInitSuperblock(void)
_lib.wInitSuperblock.argtypes = []
_lib.wInitSuperblock.restype = c_bool

# bool wInitFilesystem(void)
_lib.wInitFilesystem.argtypes = []
_lib.wInitFilesystem.restype = c_bool

# bool wGetChilds(char*** subDirs, int* size)
_lib.wGetChilds.argtypes = [
    POINTER(POINTER(c_char_p)),
    POINTER(POINTER(c_bool)),
    POINTER(c_int)
]
_lib.wGetChilds.restype = c_bool

# void wFreeChilds(char** subDirs, int size)
_lib.wFreeChilds.argtypes = [
    POINTER(c_char_p),
    POINTER(c_bool),
    c_int
]
_lib.wFreeChilds.restype = None

# bool cdToDir(char* path)
_lib.cdToDir.argtypes = [ctypes.c_char_p]
_lib.cdToDir.restype = ctypes.c_bool

# bool readFileToWindows(const char* extPath, const char* winPath)
_lib.readFileToWindows.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
_lib.readFileToWindows.restype = ctypes.c_bool


def list_disks():
    """
    Возвращает:
        (list of str, list of int) - список названий дисков и их номеров.
    """
    disks = POINTER(c_char_p)()
    disks_numbers = POINTER(c_int)()
    size = c_int()

    success = _lib.wListDisks(byref(disks), byref(disks_numbers), byref(size))
    if not success:
        raise InternalDext2Exception("wListDisks вернул false.")

    count = size.value
    disk_names = []
    disk_nums = []
    for i in range(count):
        disk_names.append(string_at(disks[i]).decode("utf-8"))
        disk_nums.append(disks_numbers[i])

    _lib.wFreeDisks(disks, disks_numbers, count)
    return disk_names, disk_nums


def init_handle(disk_num: int):
    """
    Инициализация диска по его номеру.
    """
    success = _lib.wInitHandle(disk_num)
    if not success:
        raise InternalDext2Exception(f"Не удалось инициализировать диск {disk_num}.")


def list_partitions():
    """
    Возвращает:
        (list of int, list of int) — на самом деле (list of unsigned long long, list of unsigned long long).
        Первая - offsets, вторая - lengths.
    """
    offsets = POINTER(c_ulonglong)()
    partitions_lengths = POINTER(c_ulonglong)()
    size = c_int()

    success = _lib.wListPartitions(byref(offsets), byref(partitions_lengths), byref(size))
    if not success:
        raise InternalDext2Exception("wListPartitions вернул false.")

    count = size.value
    py_offsets = []
    py_partition_lengths = []
    for i in range(count):
        py_offsets.append(offsets[i])
        py_partition_lengths.append(partitions_lengths[i])

    _lib.wFreePartitions(offsets, partitions_lengths, count)
    return py_offsets, py_partition_lengths


def init_partition(partition_start: int):
    """
    Устанавливает смещение (старт) выбранного раздела.
    """
    _lib.wInitPartition(partition_start)


def init_superblock():
    """
    Инициализирует суперблок.
    """
    success = _lib.wInitSuperblock()
    if not success:
        raise InternalDext2Exception("wInitSuperblock вернул false.")


def init_filesystem():
    """
    Инициализирует файловую систему (запрашивает root inode).
    """
    success = _lib.wInitFilesystem()
    if not success:
        raise InternalDext2Exception("wInitFilesystem вернул false.")


def get_childs():
    subdirs_ptr = POINTER(c_char_p)()
    is_dirs_ptr = POINTER(c_bool)()
    size = c_int()

    success = _lib.wGetChilds(byref(subdirs_ptr), byref(is_dirs_ptr), byref(size))
    print(success)
    if not success:
        raise InternalDext2Exception("wGetChilds вернул false.")

    count = size.value
    subdirs = [(string_at(subdirs_ptr[i]).decode().strip(), is_dirs_ptr[i]) for i in range(count)]

    _lib.wFreeChilds(subdirs_ptr, is_dirs_ptr, size)

    return subdirs

def read_file_from_ext2_to_windows(ext2_path: str, windows_path: str):
    """
    Calls the C function readFileToWindows(const char* extPath, const char* winPath).
    """
    ext2_bytes = ext2_path.encode("utf-8") + b'\0'
    win_bytes = windows_path.encode("utf-8") + b'\0'
    success = _lib.readFileToWindows(ext2_bytes, win_bytes)
    if not success:
        raise InternalDext2Exception("Ошибка при записи файла из ext2 на выбранный путь Windows.")



# def get_childs():
#     subdirs_ptr = POINTER(c_char_p)()
#     size = c_int()

#     success = _lib.wGetChilds(byref(subdirs_ptr), byref(size))
#     if not success:
#         raise InternalDext2Exception("wGetChilds returned false.")

#     count = size.value
#     subdirs = [string_at(subdirs_ptr[i]).decode().strip() for i in range(count)]

#     _lib.wFreeChilds(ctypes.cast(subdirs_ptr, POINTER(c_char_p)), size)

#     return subdirs


def cd_to_dir(path: str):
    """
    Переход в папку внутри ext2 (меняет текущую директорию).
    """
    path_bytes = path.encode("utf-8") + b'\0'
    success = _lib.cdToDir(ctypes.c_char_p(path_bytes))
    if not success:
        raise InternalDext2Exception(f"Не удалось перейти в каталог '{path}'.")


########################################################################
#                           GUI (Tkinter)                              #
########################################################################

class App(tk.Tk):
    """
    Главное окно, в котором мы переключаем «экраны» (Frame)
    путём вызова .tkraise() для нужного фрейма.
    """

    def __init__(self):
        super().__init__()
        self.title("EXT2 Explorer (Tkinter)")
        self.geometry("600x400")

        # Шрифт заголовков
        self.title_font = tkfont.Font(family="Arial", size=16, weight="bold")
        # Шрифт основного текста
        self.normal_font = tkfont.Font(family="Arial", size=12)

        # Создадим контейнер для всех фреймов
        container = tk.Frame(self)
        container.pack(side="top", fill="both", expand=True)

        # Словарь фреймов
        self.frames = {}

        # Создаём три фрейма (экрана)
        for F in (DiskSelector, PartitionSelector, Explorer):
            page_name = F.__name__
            frame = F(parent=container, controller=self)
            self.frames[page_name] = frame
            # "растягиваем" на всю площадь контейнера
            frame.grid(row=0, column=0, sticky="nsew")

        # Переходим на экран выбора диска
        self.show_frame("DiskSelector")

    def show_frame(self, page_name):
        """
        Поднимает на передний план фрейм по имени класса.
        """
        frame = self.frames[page_name]
        frame.tkraise()

    def get_page(self, page_name):
        """
        Удобный метод для доступа к фрейму (например, чтобы вызвать метод).
        """
        return self.frames[page_name]


class DiskSelector(tk.Frame):
    """
    Первый экран: выбор физического диска (C, D, E и т.д.).
    """
    def __init__(self, parent, controller):
        super().__init__(parent)
        self.controller = controller
        self.disk_nums = []

        title_label = tk.Label(self, text="Выберите диск", font=controller.title_font)
        title_label.pack(pady=10)

        self.listbox = tk.Listbox(self, font=controller.normal_font)
        self.listbox.pack(fill="both", expand=True, padx=10, pady=10)

        # Прокрутка (если хотим, чтобы листбокс прокручивался)
        scrollbar = tk.Scrollbar(self, orient="vertical", command=self.listbox.yview)
        scrollbar.pack(side="right", fill="y")
        self.listbox.config(yscrollcommand=scrollbar.set)

        # При двойном клике — выбираем диск
        self.listbox.bind("<Double-1>", self.on_item_double_click)

        # Кнопка обновления списка (на всякий случай)
        refresh_button = tk.Button(self, text="Обновить список дисков", font=controller.normal_font,
                                   command=self.load_disks)
        refresh_button.pack(pady=5)

        # Загружаем список дисков сразу
        self.load_disks()

    def load_disks(self):
        """
        Загружает список дисков из DLL и показывает в Listbox.
        """
        self.listbox.delete(0, tk.END)
        try:
            names, numbers = list_disks()
            self.disk_nums = numbers
            for name in names:
                self.listbox.insert(tk.END, name)
        except InternalDext2Exception as e:
            messagebox.showerror("Ошибка", str(e))

    def on_item_double_click(self, event):
        """
        Когда пользователь дважды кликнул по элементу списка (выбор диска).
        """
        selection = self.listbox.curselection()
        if not selection:
            return
        index = selection[0]
        disk_num = self.disk_nums[index]

        # Инициализируем диск
        try:
            init_handle(disk_num)
        except InternalDext2Exception as e:
            messagebox.showerror("Ошибка", str(e))
            return

        # Переходим на экран выбора раздела
        partition_page = self.controller.get_page("PartitionSelector")
        partition_page.load_partitions()
        self.controller.show_frame("PartitionSelector")


class PartitionSelector(tk.Frame):
    """
    Второй экран: выбор раздела. Отображает offset и размер (в МБ).
    """
    def __init__(self, parent, controller):
        super().__init__(parent)
        self.controller = controller
        self.offsets = []

        title_label = tk.Label(self, text="Выберите раздел", font=controller.title_font)
        title_label.pack(pady=10)

        self.listbox = tk.Listbox(self, font=controller.normal_font)
        self.listbox.pack(fill="both", expand=True, padx=10, pady=10)

        # Прокрутка
        scrollbar = tk.Scrollbar(self, orient="vertical", command=self.listbox.yview)
        scrollbar.pack(side="right", fill="y")
        self.listbox.config(yscrollcommand=scrollbar.set)

        self.listbox.bind("<Double-1>", self.on_item_double_click)

        # Кнопка назад (к дискам), если нужно
        back_button = tk.Button(self, text="Назад к списку дисков", font=controller.normal_font,
                                command=lambda: controller.show_frame("DiskSelector"))
        back_button.pack(pady=5)

    def load_partitions(self):
        """
        Загружает список разделов и показывает их с размером в МБ.
        """
        self.listbox.delete(0, tk.END)
        try:
            offs, lengths = list_partitions()
            self.offsets = offs
            for i in range(len(offs)):
                offset_val = offs[i]
                length_val = lengths[i]
                length_mb = length_val / (1024 * 1024)
                text = f"Offset: {offset_val} | Size: {length_mb:.2f} MB"
                self.listbox.insert(tk.END, text)
        except InternalDext2Exception as e:
            messagebox.showerror("Ошибка", str(e))

    def on_item_double_click(self, event):
        """
        Когда пользователь дважды кликнул по выбранному разделу.
        """
        selection = self.listbox.curselection()
        if not selection:
            return
        index = selection[0]
        offset = self.offsets[index]

        # Инициализируем раздел, суперблок, ФС
        try:
            init_partition(offset)
            init_superblock()
            init_filesystem()
        except InternalDext2Exception as e:
            messagebox.showerror("Ошибка", str(e))
            return

        # Переходим к "проводнику"
        explorer_page = self.controller.get_page("Explorer")
        explorer_page.load_root_directory()
        self.controller.show_frame("Explorer")


# class Explorer(tk.Frame):
#     """
#     Третий экран: показывает содержимое текущего каталога (папки и файлы).
#     По двойному клику по папке — заходим в неё.
#     """
#     def __init__(self, parent, controller):
#         super().__init__(parent)
#         self.controller = controller

#         title_label = tk.Label(self, text="Проводник по ext2", font=controller.title_font)
#         title_label.pack(pady=10)

#         self.listbox = tk.Listbox(self, font=controller.normal_font)
#         self.listbox.pack(fill="both", expand=True, padx=10, pady=10)

#         scrollbar = tk.Scrollbar(self, orient="vertical", command=self.listbox.yview)
#         scrollbar.pack(side="right", fill="y")
#         self.listbox.config(yscrollcommand=scrollbar.set)

#         self.listbox.bind("<Double-1>", self.on_item_double_click)

#         # Кнопка обновить
#         refresh_button = tk.Button(self, text="Обновить", font=controller.normal_font,
#                                    command=self.refresh)
#         refresh_button.pack(pady=5)


#     def load_root_directory(self):
#         """
#         При первом входе в Explorer загружаем текущий каталог (должен быть корень).
#         """
#         self.refresh()

#     def refresh(self):
#         """
#         Перезагружает текущий список директорий/файлов.
#         """
#         self.listbox.delete(0, tk.END)
#         try:
#             children = get_childs()
#             for child in children:
#                 if not child[1]:
#                     self.listbox.insert(tk.END, f"Файл: {child[0]}")
#                 else:
#                     self.listbox.insert(tk.END, f"Папка: {child[0]}")
#         except InternalDext2Exception as e:
#             messagebox.showerror("Ошибка", str(e))

#     def on_item_double_click(self, event):
#         """
#         По двойному клику проверяем, папка это или файл; если папка — заходим.
#         """
#         selection = self.listbox.curselection()
#         if not selection:
#             return
#         index = selection[0]
#         text = self.listbox.get(index)

#         if text.startswith("Папка: "):
#             folder_name = text.replace("Папка: ", "").strip()
#             try:
#                 cd_to_dir(folder_name)
#                 self.refresh()
#             except InternalDext2Exception as e:
#                 messagebox.showerror("Ошибка", str(e))
#         else:
#             # Если это файл, можно реализовать чтение, копирование и т.п.
#             messagebox.showinfo("Файл", f"Это файл: {text}")


class Explorer(tk.Frame):
    """
    Проводник по ext2. В этом классе добавим логику «сохранить файл в Windows».
    """
    def __init__(self, parent, controller):
        super().__init__(parent)
        self.controller = controller

        title_label = tk.Label(self, text="Проводник по ext2", font=controller.title_font)
        title_label.pack(pady=10)

        self.listbox = tk.Listbox(self, font=controller.normal_font)
        self.listbox.pack(fill="both", expand=True, padx=10, pady=10)

        scrollbar = tk.Scrollbar(self, orient="vertical", command=self.listbox.yview)
        scrollbar.pack(side="right", fill="y")
        self.listbox.config(yscrollcommand=scrollbar.set)

        # Double-click to either enter folder or save file
        self.listbox.bind("<Double-1>", self.on_item_double_click)

        refresh_button = tk.Button(self, text="Обновить", font=controller.normal_font,
                                   command=self.refresh)
        refresh_button.pack(pady=5)

    def load_root_directory(self):
        self.refresh()

    def refresh(self):
        self.listbox.delete(0, tk.END)
        try:
            children = get_childs()
            for child in children:
                name, is_dir = child
                if is_dir:
                    self.listbox.insert(tk.END, f"Папка: {name}")
                else:
                    self.listbox.insert(tk.END, f"Файл: {name}")
        except InternalDext2Exception as e:
            messagebox.showerror("Ошибка", str(e))

    def on_item_double_click(self, event):
        """
        По двойному клику:
          - Если это папка, перейти в неё.
          - Если это файл, открыть диалог "Сохранить как..." и скопировать файл на Windows.
        """
        selection = self.listbox.curselection()
        if not selection:
            return
        index = selection[0]
        text = self.listbox.get(index)

        if text.startswith("Папка: "):
            folder_name = text.replace("Папка: ", "").strip()
            try:
                cd_to_dir(folder_name)
                self.refresh()
            except InternalDext2Exception as e:
                messagebox.showerror("Ошибка", str(e))
        else:
            # NEW: On file click, prompt the user for a save path in Windows
            file_name = text.replace("Файл: ", "").strip()

            # Let user pick a Windows file path using the standard “Save As…” dialog
            chosen_win_path = filedialog.asksaveasfilename(
                title="Сохранить файл из ext2...",
                initialfile=file_name  # you can set a default name
            )
            if chosen_win_path:  # if user didn’t cancel
                try:
                    # Copy from ext2 to the chosen path in Windows
                    read_file_from_ext2_to_windows(file_name, chosen_win_path)
                    messagebox.showinfo("Успех", f"Файл успешно сохранён:\n{chosen_win_path}")
                except InternalDext2Exception as e:
                    messagebox.showerror("Ошибка", str(e))


def main():
    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
