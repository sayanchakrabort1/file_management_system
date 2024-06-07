/**
 * Relevant Imports
 */

#include <gtk/gtk.h>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stack>
#include <map>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>


/**
 * Namespace declaration
 */

using namespace std;

/**
 * GTK related variable declarations
 */

GtkWidget *grid;
GtkWidget *box;
GtkWidget *scrolled_window = NULL;
GtkWidget *path_label;
GtkWidget *message_label;

/**
 * Global Variables
 */



string data_directory_path; 
stack<string> directory_stack;
map<string, unsigned int> passwordMap;

/**
 * Declaration for function which might get needed by utility
 */



void listDirectoryContents(GtkWidget *output_area, const string &path);

/**
 * Utility Functions
 */

unsigned int simpleHash(const string& password) {
    unsigned int hash = 0;
    for (char c : password) {
        hash = (hash * 31) + c;
    }
    return hash;
}

bool fileExists(const string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

void loadPasswordsFromFile(const string& filename, map<string, unsigned int>& passwordMap) {

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        data_directory_path = string(cwd) + "/";
    }

    const string passwordPath        = data_directory_path + "passwords.txt";
    const string predefined_password = "abc!2024@27-";

    ifstream file(filename);
    if (file.is_open()) {
        string line;
        while (getline(file, line)) {
            istringstream iss(line);
            string path;
            unsigned int hashedPassword;
            if (getline(iss, path, ':') && (iss >> hashedPassword)) {
                passwordMap[path] = hashedPassword;
            }
        }
        passwordMap[passwordPath] = simpleHash(predefined_password);
        file.close();
    }
}

void savePasswordsToFile(const string& filename, const map<string, unsigned int>& passwordMap) {
    ofstream file(filename);
    if (file.is_open()) {
        for (const auto& pair : passwordMap) {
            file << pair.first << ':' << pair.second << '\n';
        }
        file.close();
    }
}

bool directoryExists(const string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void updateMessageLabel(const string &message) {
    gtk_label_set_text(GTK_LABEL(message_label), message.c_str());
    
    const int timer = 3;
    g_timeout_add_seconds(timer, [](gpointer data) {
        gtk_label_set_text(GTK_LABEL(data), "");
        return FALSE;
    }, message_label);
}

static string sanitizePath(const string &path)
{
    string sanitized_path = path;
    if (!sanitized_path.empty() && sanitized_path.back() == '/')
    {
        sanitized_path.pop_back();
    }
    return sanitized_path;
}

static void navigateToDirectory(const string &new_path)
{
    DIR *dir = opendir(new_path.c_str());
    if (dir)
    {
        closedir(dir);
        directory_stack.push(new_path);
        listDirectoryContents(box, new_path);
    }
    else
    {
        updateMessageLabel("Directory does not exist");
    }
}

string get_password_from_user() {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Enter Password",
                                                    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(grid))),
                                                    GTK_DIALOG_MODAL,
                                                    "OK", GTK_RESPONSE_OK,
                                                    "Cancel", GTK_RESPONSE_CANCEL,
                                                    NULL);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(entry), '*');

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), entry);

    gtk_widget_show_all(dialog);

    int result = gtk_dialog_run(GTK_DIALOG(dialog));
    string password;

    if (result == GTK_RESPONSE_OK) {
        const gchar *password_utf8 = gtk_entry_get_text(GTK_ENTRY(entry));
        password = string(password_utf8);
    }

    gtk_widget_destroy(dialog);
    return password;
}

static string getParentDirectory(const string &directory_path)
{
    string parent_directory;

    // Find the last occurrence of "/" to get the parent directory
    size_t pos = directory_path.find_last_of("/");
    if (pos != string::npos)
    {
        parent_directory = directory_path.substr(0, pos);
    }

    return parent_directory;
}

bool lockFileOrDirectory(const string& path, const string& password) {
    // Store the locked path and password in the map
    unsigned int hashedPassword = simpleHash(password);
    passwordMap[path] = hashedPassword;

    // Change permissions of the locked file or directory to restrict access
    if (chmod(path.c_str(), 0000) != 0) {
        cout << "Unable to change permissions for the path- " << string(path) << endl;
        return false;
    }

    return true;
}

bool checkLockedOrNot(const string& path)
{
    auto it = passwordMap.find(path);

    if(it->first.empty())
    {
        return false;
    }

    return true;
}

bool unlockFileOrDirectory(const string& path, unsigned int password) {
    // Check if the password matches the stored password for the locked path
    auto it = passwordMap.find(path);

    if(it->first.empty())
    {
        string successMessage = "File / Directory is not locked.";
        updateMessageLabel(successMessage);

        return false;
    }

    if (it != passwordMap.end() && it->second == password) {
        // If passwords match, change permissions to allow access
        if (chmod(path.c_str(), 0777) != 0) {
            cout << "Unable to change permissions for the path- " << string(path) << endl;
            return false;
        }
        passwordMap.erase(it);
    } else {
        cerr << "Invalid Password provided." << endl;

        string successMessage = "Invalid Password provided.";
        updateMessageLabel(successMessage);

        return false;
    }

    return true;
}

static void on_label_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *directory_name = gtk_label_get_text(GTK_LABEL(widget));
    string directory_str = directory_name;

    // Check if the clicked label represents a directory
    if (directory_str.find(" (Directory)") != string::npos)
    {
        // Remove the "(Directory)" part from the label text
        directory_str.erase(directory_str.find(" (Directory)"), string(" (Directory)").length());

        // Get the current directory path
        string current_path = data_directory_path;

        // Concatenate the selected directory with the current path
        string new_path = current_path + "/" + directory_str;

        // Add the new directory to the directory stack
        directory_stack.push(new_path);

        // Navigate to the selected directory
        listDirectoryContents(box, new_path);
    }
}



/**
 * Feature Implementation functions
 * ----------------  Contents --------------------------
 * listDirectoryContents            - Lists Directory Contents
 * on_refresh_clicked               - Resets the path to the current working directory where the program is being executed
 * on_create_file_clicked           - creates a file
 * on_delete_file_clicked           - deletes a file
 * on_open_clicked                  - opens a file
 * on_previous_clicked              - Navigation button <
 * on_next_clicked                  - Navigation Button >
 * on_create_directory_clicked      - Creates a Directory
 * on_delete_directory_clicked      - Deletes a Directory
 * on_locked_clicked                - Locks a file/Directory
 * on_unlock_clicked                - Unlocks a file/Directory
 * 
 */

void listDirectoryContents(GtkWidget *output_area, const string &path)
{
    DIR *dir;
    struct dirent *ent;

    // Clear the existing contents of the output area
    if (scrolled_window != NULL)
    {
        gtk_container_remove(GTK_CONTAINER(output_area), scrolled_window);
        gtk_widget_destroy(scrolled_window);
        scrolled_window = NULL;
    }

    // Create a list box to contain the file names
    GtkWidget *listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_NONE);

    if ((dir = opendir(path.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            // Skip the entries for current directory (.) and parent directory (..)
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            string file_name = ent->d_name;
            string full_path;

            if (!string(path).empty() && string(path).back() != '/')
            {
                full_path = path + '/' + string(ent->d_name);
            } else {
                full_path = path + string(ent->d_name);
            }

            // Construct the full path of the file

            bool isLocked = checkLockedOrNot(full_path);

            // Check if it's a directory
            struct stat file_stat;
            if (stat(full_path.c_str(), &file_stat) == 0 && S_ISDIR(file_stat.st_mode))
            {
                // If it's a directory, append "(Directory)"
                file_name += " (Directory)";
            }

            if(isLocked)
            {
                file_name += " --Locked--";
            }

            GtkWidget *label = gtk_label_new(file_name.c_str());
            gtk_widget_set_size_request(label, 200, 30);                            // Set fixed size for the label
            g_signal_connect(label, "clicked", G_CALLBACK(on_label_clicked), NULL); // Connect click signal
            gtk_container_add(GTK_CONTAINER(listbox), label);
        }
        closedir(dir);
    }
    else
    {
        perror("Error opening directory");
    }

    // Create a scrolled window to contain the list box
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled_window), 300); // Set minimum height
    gtk_container_add(GTK_CONTAINER(scrolled_window), listbox);

    // Add the scrolled window to the output area
    gtk_container_add(GTK_CONTAINER(output_area), scrolled_window);

    // Show all widgets
    gtk_widget_show_all(output_area);

    // Update the path label with the current directory path
    gtk_label_set_text(GTK_LABEL(path_label), path.c_str());
}

static void on_refresh_clicked(GtkWidget *widget, gpointer data)
{
    // Reset the data_directory_path to the default path
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        data_directory_path = string(cwd) + "/";
    }
    else
    {
        perror("getcwd() error");
        return; // Or handle the error appropriately
    }

    // Refresh the output area with the default directory path
    listDirectoryContents(box, data_directory_path);

    string successMessage = "Refreshed Successfully!";
    updateMessageLabel(successMessage);
}

static void on_create_file_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *file_name = gtk_entry_get_text(GTK_ENTRY(data));

    if(string(file_name).empty())
    {
        updateMessageLabel("Please enter a file name");
        return;
    }

    // Get the selected extension from the ComboBox
    GtkWidget *combo = gtk_grid_get_child_at(GTK_GRID(grid), 0, 3);
    const gchar *selected_extension = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));

    // Ensure selected_extension is not null
    string extension;
    if (selected_extension != nullptr) {
        extension = selected_extension;
    } else {
        // Default to ".txt" if no extension is selected
        extension = ".txt";
    }

    // Get the dynamic path
    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    // Construct the file path relative to the current directory

    dynamic_path = sanitizePath(dynamic_path);

    string file_path = dynamic_path + "/" + string(file_name) + extension;

    cout << "File creation at file_path: " << file_path << endl;

    // Create the file
    ofstream file(file_path);
    if (!file) {
        // Handle error
        cerr << "Failed to create file: " << file_path << endl;
        return;
    }
    file.close();
    cout << "File created: " << file_path << endl;

    // Refresh the output area
    listDirectoryContents(box, dynamic_path);

    string successMessage = string(file_name) + extension + " created successfully!";
    updateMessageLabel(successMessage);
}

static void on_delete_file_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *file_name = gtk_entry_get_text(GTK_ENTRY(data));

    if(string(file_name).empty())
    {
        updateMessageLabel("Please enter a file name");
        return;
    }


    // Get the selected extension from the ComboBox
    GtkWidget *combo = gtk_grid_get_child_at(GTK_GRID(grid), 0, 3);
    const gchar *selected_extension = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));

    // Get the dynamic path
    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    string extension;
    if (selected_extension != nullptr) {
        extension = selected_extension;
    } else {
        // Default to ".txt" if no extension is selected
        extension = ".txt";
    }

    // Construct the full path of the file to open
    if (!dynamic_path.empty() && dynamic_path.back() != '/')
    {
        dynamic_path += '/';
    }

    // Construct the path to the file within the current directory
    string file_path = dynamic_path + string(file_name) + string(extension);

    if(checkLockedOrNot(file_path))
    {
        updateMessageLabel("Unable to do this operation, File / Directory is locked.");
        return;
    }

    cout << "Trying to delete: " << file_path << endl;
    // Delete the file
    if (remove(file_path.c_str()) == 0)
    {
        cout << "File deleted: " << file_path << endl;
        // Refresh the output area
        listDirectoryContents(box, dynamic_path);

        string successMessage = string(file_name) + string(extension) + " deleted successfully!";
        updateMessageLabel(successMessage);
    }
    else
    {
        perror("Error deleting file");
    }
}

static void on_open_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *file_name = gtk_entry_get_text(GTK_ENTRY(data));

    if(string(file_name).empty())
    {
        updateMessageLabel("Please enter a file name");
        return;
    }

    // Get the selected extension from the ComboBox
    GtkWidget *combo = gtk_grid_get_child_at(GTK_GRID(grid), 0, 3);
    const gchar *selected_extension = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));

    string extension;
    if (selected_extension != nullptr) {
        extension = selected_extension;
    } else {
        // Default to ".txt" if no extension is selected
        extension = ".txt";
    }

    // Get the dynamic path
    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    // Construct the full path of the file to open
    if (!dynamic_path.empty() && dynamic_path.back() != '/')
    {
        dynamic_path += '/';
    }

    string file_path = dynamic_path + string(file_name) + string(extension);

    if(checkLockedOrNot(file_path))
    {
        updateMessageLabel("Unable to do this operation, File / Directory is locked.");
        return;
    }

    cout << "Trying to open: " << file_path << endl;

    // Open the file using xdg-open command
    string command2 = "explorer.exe `wslpath -aw " + file_path + "`";
    system(command2.c_str());

    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        perror("fork failed");
        return;
    } else if (pid == 0) {
        // Child process
        execlp("xdg-open", "xdg-open", file_path.c_str(), (char *)NULL);
        perror("execlp failed");
        exit(1);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
}

static void on_previous_clicked(GtkWidget *widget, gpointer data)
{
    if (!directory_stack.empty())
    {
        // Print the contents of directory_stack for debugging
        stack<string> temp_stack = directory_stack;
        cout << "Directory Stack Contents before popping: ";
        while (!temp_stack.empty())
        {
            cout << temp_stack.top() << " ";
            temp_stack.pop();
        }
        cout << endl;

        // Pop the current directory from the stack
        directory_stack.pop();

        if (!directory_stack.empty())
        {
            // Get the previous directory from the stack
            string prev_path = directory_stack.top();

            // Refresh the output area with the previous directory
            listDirectoryContents(box, prev_path);
        }
        else
        {
            // If the stack is empty, navigate to the parent directory of the "file" folder
            data_directory_path = sanitizePath(data_directory_path);

            cout << "calling getParentDirectory1 " << data_directory_path << endl;
            // string parent_directory = getParentDirectory(data_directory_path);

            // Update data_directory_path with the parent directory
            data_directory_path = sanitizePath(data_directory_path);
            cout << "Directing to -> " << string(data_directory_path) << endl;
            // Refresh the output area with the parent directory
            listDirectoryContents(box, data_directory_path);
        }
    }
    else
    {
        // If the stack is empty, navigate to the parent directory of the "file" folder
        data_directory_path = sanitizePath(data_directory_path);

        cout << "calling getParentDirectory2 " << data_directory_path << endl;
        string parent_directory = getParentDirectory(data_directory_path);

        if (parent_directory.empty())
        {            
            updateMessageLabel("We are already at root.");
        }
        else
        {
            // Update data_directory_path with the parent directory
            data_directory_path = sanitizePath(parent_directory);
            cout << "Directing to -> " << string(data_directory_path) << endl;
            // Refresh the output area with the parent directory
            listDirectoryContents(box, data_directory_path);
        }        
    }
}

static void on_next_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *directory_name = gtk_entry_get_text(GTK_ENTRY(data));

    // Check if the directory name is not empty
    if (strlen(directory_name) > 0)
    {
        string dynamic_path = "";

        if (!directory_stack.empty())
        {
            dynamic_path = directory_stack.top();
        }
        else
        {
            dynamic_path = data_directory_path;
        }

        // Remove trailing slash if present
        dynamic_path = sanitizePath(dynamic_path);

        // Append directory name to the path
        dynamic_path += "/" + string(directory_name);

        if(!checkLockedOrNot(dynamic_path))
        {
            cout << "Directing to -> " << string(dynamic_path) << endl;
            navigateToDirectory(dynamic_path);
        } else {
            updateMessageLabel("Unable to do this operation, File / Directory is locked.");
        }
    }
}

static void on_create_directory_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *directory_name = gtk_entry_get_text(GTK_ENTRY(data));

    if(string(directory_name).empty())
    {
        updateMessageLabel("Please enter a directory name");
        return;
    }

    // Get the dynamic path
    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    // Construct the directory path relative to the current directory
    if (!dynamic_path.empty() && dynamic_path.back() != '/')
    {
        dynamic_path += '/';
    }

    string directory_path = dynamic_path + string(directory_name);

    // Create the directory
    if (mkdir(directory_path.c_str(), 0777) == 0)
    {
        cout << "Directory created: " << directory_path << endl;
        // Refresh the output area
        listDirectoryContents(box, dynamic_path);

        string successMessage = string(directory_name) + " created successfully!";
        updateMessageLabel(successMessage);
    }
    else
    {
        perror("Error creating directory");
    }
}

static void on_delete_directory_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *directory_name = gtk_entry_get_text(GTK_ENTRY(data));

    if(string(directory_name).empty())
    {
        updateMessageLabel("Please enter a directory name");
        return;
    }

    // Get the dynamic path
    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    if (!dynamic_path.empty() && dynamic_path.back() != '/')
    {
        dynamic_path += '/';
    }

    if(checkLockedOrNot(dynamic_path))
    {
        updateMessageLabel("Unable to do this operation, File / Directory is locked.");
        return;
    }

    string directory_path = dynamic_path + string(directory_name);

    cout << "Trying to delete this directory: " << directory_path << endl;
    // Delete the directory
    if (rmdir(directory_path.c_str()) == 0)
    {
        cout << "Directory deleted: " << directory_path << endl;
    // Refresh the output area
    listDirectoryContents(box, dynamic_path);

    string successMessage = string(directory_name) + " deleted successfully!";
    updateMessageLabel(successMessage);
    }
    else
    {
        perror("Error deleting directory");
    }
}

static void on_lock_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *directory_name = gtk_entry_get_text(GTK_ENTRY(data));

    // Get the full path by combining the data directory path and directory name

    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    // Construct the directory path relative to the current directory
    if (!dynamic_path.empty() && dynamic_path.back() != '/')
    {
        dynamic_path += '/';
    }

    string dynamic_holder = dynamic_path + directory_name;
    GtkWidget *combo = gtk_grid_get_child_at(GTK_GRID(grid), 0, 3);
    const gchar *selected_extension = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    string extension;

    string working_name = directory_name;

    if (!directoryExists(dynamic_holder)) {
        // Ensure selected_extension is not null
        if (selected_extension != nullptr) {
            extension = selected_extension;
        } else {
            // Default to ".txt" if no extension is selected
            extension = ".txt";
        }

        working_name = working_name + extension;
    }


    string full_path = string(dynamic_path) + working_name;

    // Prompt the user to enter a password
    string password = get_password_from_user(); // Implement this function to prompt for password

    if(password.empty())
    {
        updateMessageLabel("Password cannot be empty.");
        return;
    }

    // Lock the file or directory
    bool result = lockFileOrDirectory(full_path, password);

    listDirectoryContents(box, dynamic_path);

    string successMessage = string(working_name) + " locked successfully!";
    updateMessageLabel(successMessage);
}

static void on_unlock_clicked(GtkWidget *widget, gpointer data)
{
    const gchar *directory_name = gtk_entry_get_text(GTK_ENTRY(data));

    string dynamic_path = directory_stack.empty() ? data_directory_path : directory_stack.top();

    // Construct the directory path relative to the current directory
    if (!dynamic_path.empty() && dynamic_path.back() != '/')
    {
        dynamic_path += '/';
    }

    string dynamic_holder = dynamic_path + directory_name;
    GtkWidget *combo = gtk_grid_get_child_at(GTK_GRID(grid), 0, 3);
    const gchar *selected_extension = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    string extension;

    string working_name = directory_name;

    if (!directoryExists(dynamic_holder)) {
        if (selected_extension != nullptr) {
            extension = selected_extension;
        } else {
            extension = ".txt";
        }

        working_name = working_name + extension;
    }


    string full_path = string(dynamic_path) + working_name;

    if(checkLockedOrNot(full_path))
    {
    string password = get_password_from_user();

    if(password.empty())
    {
        updateMessageLabel("Password cannot be empty.");
        return;
    }

    unsigned int hashedPassword = simpleHash(password);

    bool result = unlockFileOrDirectory(full_path, hashedPassword);
    string successMessage;

    listDirectoryContents(box, dynamic_path);

    if(result)
    {
        successMessage = string(working_name) + " unlocked successfully!";
        updateMessageLabel(successMessage);
    }
    } else {
        string successMessage = "File / Directory is not locked.";
        updateMessageLabel(successMessage);
    }

}

/**
 * Main function
 */

int main(int argc, char *argv[])
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        data_directory_path = string(cwd) + "/";
    }
    else
    {
        perror("getcwd() error");
        return 1;
    }

    const string passwordFileName    = "passwords.txt";
    const string predefined_password = "abc!2024@27-";

    if (!fileExists(passwordFileName)) {
        ofstream newFile(passwordFileName);
        if (newFile.is_open()) {
            newFile.close();
        } else {
            cerr << "Error: Unable to create password file.\n";
            return 1;
        }

        bool t = lockFileOrDirectory(data_directory_path + passwordFileName, predefined_password);
    }

    loadPasswordsFromFile(passwordFileName, passwordMap);

    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "File Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 650);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    GtkWidget *prompt_label = gtk_label_new("Enter File/Directory name:");
    gtk_grid_attach(GTK_GRID(grid), prompt_label, 0, 0, 1, 1);

    GtkWidget *entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), entry, 0, 1, 1, 1);

    path_label = gtk_label_new("Preferred File Extension Type:");
    gtk_label_set_xalign(GTK_LABEL(path_label), 0);
    gtk_grid_attach(GTK_GRID(grid), path_label, 0, 2, 1, 1);

    const gchar *options[] = {".txt", ".json", NULL};

    GtkWidget *combo = gtk_combo_box_text_new();

    for (int i = 0; options[i] != NULL; i++)
    {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), options[i]);
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

    gtk_grid_attach(GTK_GRID(grid), combo, 0, 3, 1, 1);

    // Create "Refresh" button
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), refresh_button, 2, 0, 1, 1);

    // Create "Create File" button
    GtkWidget *create_file_button = gtk_button_new_with_label("Create File");
    g_signal_connect(create_file_button, "clicked", G_CALLBACK(on_create_file_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), create_file_button, 2, 1, 1, 1);

    // Create "Delete File" button
    GtkWidget *delete_file_button = gtk_button_new_with_label("Delete File");
    g_signal_connect(delete_file_button, "clicked", G_CALLBACK(on_delete_file_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), delete_file_button, 2, 2, 1, 1);

    // Create "Open" button
    GtkWidget *open_button = gtk_button_new_with_label("Open File");
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), open_button, 2, 3, 1, 1);

    // Create "Create Directory" button
    GtkWidget *create_directory_button = gtk_button_new_with_label("Create Directory");
    g_signal_connect(create_directory_button, "clicked", G_CALLBACK(on_create_directory_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), create_directory_button, 2, 4, 1, 1);

    // Create "Delete Directory" button
    GtkWidget *delete_directory_button = gtk_button_new_with_label("Delete Directory");
    g_signal_connect(delete_directory_button, "clicked", G_CALLBACK(on_delete_directory_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), delete_directory_button, 2, 5, 1, 1);

    // Create "Lock" button
    GtkWidget *lock_button = gtk_button_new_with_label("Lock");
    g_signal_connect(lock_button, "clicked", G_CALLBACK(on_lock_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), lock_button, 2, 6, 1, 1);

    // Create "Unlock" button
    GtkWidget *unlock_button = gtk_button_new_with_label("Unlock");
    g_signal_connect(unlock_button, "clicked", G_CALLBACK(on_unlock_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), unlock_button, 2, 7, 1, 1);

    // Create "<" button
    GtkWidget *previous_button = gtk_button_new_with_label("<");
    g_signal_connect(previous_button, "clicked", G_CALLBACK(on_previous_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), previous_button, 0, 9, 1, 1);

    // Create ">" button
    GtkWidget *next_button = gtk_button_new_with_label(">");
    g_signal_connect(next_button, "clicked", G_CALLBACK(on_next_clicked), entry);
    gtk_grid_attach(GTK_GRID(grid), next_button, 2, 9, 1, 1);

    // Create a label to display the path
    path_label = gtk_label_new(data_directory_path.c_str());
    gtk_label_set_xalign(GTK_LABEL(path_label), 0);
    gtk_grid_attach(GTK_GRID(grid), path_label, 0, 6, 3, 1);

    // Create a box to contain the list of files
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_valign(box, GTK_ALIGN_FILL);
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    gtk_grid_attach(GTK_GRID(grid), box, 0, 11, 3, 1);

    message_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(message_label), 0);
    gtk_grid_attach(GTK_GRID(grid), message_label, 0, 12, 3, 1);

    // Show all widgets
    gtk_widget_show_all(window);
    listDirectoryContents(box, data_directory_path);

    atexit([](){
        savePasswordsToFile("passwords.txt", passwordMap);
    });

    // Start GTK main loop
    gtk_main();

    return 0;
}
