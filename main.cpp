#include <cstddef>
#include <gmodule.h>
#include <gtk/gtk.h>

#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <mfind/mfind.hpp>

std::mutex boxmutex;

std::string exec(const char *cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

GtkBuilder *builder = NULL;
GtkWidget *window = NULL;

// Handle the user trying to close the window
gboolean windowDelete(__attribute__((unused)) GtkWidget *widget,
                      __attribute__((unused)) GdkEvent *event,
                      __attribute__((unused)) gpointer data) {
  g_print("%s called.\n", __FUNCTION__);
  return TRUE; // Returning TRUE stops the window being deleted.
               // Returning FALSE allows deletion.
}

std::vector<std::string> splitString(const std::string &str) {
  std::vector<std::string> tokens;

  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, '\n')) {
    tokens.push_back(token);
  }

  return tokens;
}

void clearContainer(GtkWidget *container) {
  GList *children, *iter;

  children = gtk_container_get_children(GTK_CONTAINER(container));
  for (iter = children; iter != NULL; iter = g_list_next(iter))
    gtk_widget_destroy(GTK_WIDGET(iter->data));
  g_list_free(children);
}

extern "C" G_MODULE_EXPORT void
getSearch(__attribute__((unused)) GtkWidget *widget,
          __attribute__((unused)) GdkEvent *event,
          __attribute__((unused)) gpointer data) {
  const char *content = gtk_entry_get_text(GTK_ENTRY(widget));
  boxmutex.lock();
  clearContainer(GTK_WIDGET(gtk_builder_get_object(builder, "DisplayBox")));
  boxmutex.unlock();
  gtk_window_resize(GTK_WINDOW(window), 700, 400);

  std::regex empty("^\\s*$");

  if (std::regex_match(content, empty)) {
    return;
  }

  if (gtk_toggle_button_get_active(
          GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "RegexButton"))))
    ; // do regex-ey things here.

  std::string search = std::string(".*") + content + ".*";

  // std::string command =
  //     std::string(&"find . -wholename '*"[0]) + content + "*'";

  // std::string rawfiles = exec(command.c_str());

  // std::vector<std::string> files = splitString(rawfiles);

  // mfind_tpool.stop();
  // mfind_tpool.stop();
  // mfind_tpool.resize(std::thread::hardware_concurrency() * 4);
  mfind_search = search;
  re = std::regex(mfind_search);
  mfind_cancel = true;
  usleep(100000);
  mfind_cancel = false;
  mfind_tpool.push(processDirectory, std::string("."));
}

const char *GetItemLabel(GtkWidget *widget) {
  GtkWidget *label = NULL;
  const char *str = NULL;
  GList *children;
  GtkWidget *child;

  /* --- Get list of children in the list item --- */
  children = gtk_container_get_children(GTK_CONTAINER(widget));

  /* --- Get the widget --- */
  child = GTK_WIDGET(children->data);

  /* --- If the widget is a label --- */
  if (GTK_IS_LABEL(child)) {

    /* --- Get the string in the label --- */
    str = gtk_label_get_text(GTK_LABEL(child));
  }

  /* --- Return the string --- */
  return str;
}

std::string getDirectory(std::string file) {
  std::string directory;
  const size_t last_slash_idx = file.rfind('/');
  if (std::string::npos != last_slash_idx) {
    directory = file.substr(0, last_slash_idx);
  }

  return directory;
}

extern "C" G_MODULE_EXPORT void
openFile(__attribute__((unused)) GtkWidget *widget,
         __attribute__((unused)) GdkEvent *event,
         __attribute__((unused)) gpointer data) {
  /*GtkListBoxRow *w = gtk_list_box_get_selected_row(GTK_LIST_BOX(widget));*/

  int r = system((std::string("xdg-open \"") +
                  GetItemLabel(GTK_WIDGET(
                      gtk_list_box_get_selected_row(GTK_LIST_BOX(widget)))) +
                  "\"")
                     .c_str());
  gtk_main_quit();
}

bool cancel_last = false;

bool addResults() {
  GtkListBox *box = GTK_LIST_BOX(gtk_builder_get_object(builder, "DisplayBox"));
  /*if (mfind_cancel == cancel_last) {
    mfind_cancel = false;
    return 1;
  }*/

  if (!mfind_buffer.size()) {
    return 1;
  }

  if (mfind_buffer.size() > 1000) {
    boxmutex.lock();
    clearContainer(GTK_WIDGET(gtk_builder_get_object(builder, "DisplayBox")));
    GtkWidget *label =
        gtk_label_new("Too many results - please narrow search (or find file "
                      "manually.)");

    gtk_widget_show(label);

    gtk_list_box_insert(box, label, -1);
    boxmutex.unlock();
    outputmutex.lock();
    mfind_buffer.erase(mfind_buffer.begin(), mfind_buffer.end());
    outputmutex.unlock();
  }

  outputmutex.lock();
  boxmutex.lock();
  for (auto &i : mfind_buffer) {
    std::string file = i;
    GtkWidget *label = gtk_label_new(file.c_str());

    gtk_widget_show(label);

    gtk_list_box_insert(box, label, -1);
  }
  mfind_buffer.erase(mfind_buffer.begin(), mfind_buffer.end());
  outputmutex.unlock();
  boxmutex.unlock();

  cancel_last = mfind_cancel;

  return 1;
}

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  builder = gtk_builder_new();

  if (gtk_builder_add_from_file(
          builder, (getDirectory(argv[0]) + "/Glade/FileSearch.glade").c_str(),
          NULL) == 0) {
    printf("gtk_builder_add_from_file FAILED\n");
    return (0);
  }

  window = GTK_WIDGET(gtk_builder_get_object(builder, "FileSearchDialog"));
  gtk_builder_connect_signals(builder, NULL);
  gtk_widget_show_all(window);
  g_timeout_add(250, G_SOURCE_FUNC(addResults), NULL);
  gtk_main();
  std::exit(0);
  return 0;
}