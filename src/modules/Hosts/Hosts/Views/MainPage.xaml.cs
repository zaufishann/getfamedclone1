﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Threading.Tasks;
using System.Windows.Input;
using CommunityToolkit.Mvvm.Input;
using Hosts.Models;
using Hosts.Settings;
using Hosts.ViewModels;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.ApplicationModel.Resources;

namespace Hosts.Views
{
    public sealed partial class MainPage : Page
    {
        public MainViewModel ViewModel { get; private set; }

        public ICommand NewDialogCommand => new AsyncRelayCommand(OpenNewDialogAsync);

        public ICommand AdditionalLinesDialogCommand => new AsyncRelayCommand(OpenAdditionalLinesDialogAsync);

        public ICommand AddCommand => new RelayCommand(Add);

        public ICommand UpdateCommand => new RelayCommand(Update);

        public ICommand DeleteCommand => new RelayCommand(Delete);

        public ICommand UpdateAdditionalLinesCommand => new RelayCommand(UpdateAdditionalLines);

        public ICommand ExitCommand => new RelayCommand(() => { Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread().TryEnqueue(Application.Current.Exit); });

        public MainPage()
        {
            InitializeComponent();
            ViewModel = App.GetService<MainViewModel>();
            DataContext = ViewModel;
        }

        private async Task OpenNewDialogAsync()
        {
            var resourceLoader = ResourceLoader.GetForViewIndependentUse();
            EntryDialog.Title = resourceLoader.GetString("AddNewEntryTitle");
            EntryDialog.PrimaryButtonText = resourceLoader.GetString("Add");
            EntryDialog.PrimaryButtonCommand = AddCommand;
            EntryDialog.DataContext = new Entry(string.Empty, string.Empty, string.Empty, true);
            await EntryDialog.ShowAsync();
        }

        private async Task OpenAdditionalLinesDialogAsync()
        {
            AdditionalLines.Text = ViewModel.AdditionalLines;
            await AdditionalLinesDialog.ShowAsync();
        }

        private async void Entries_ItemClick(object sender, ItemClickEventArgs e)
        {
            var resourceLoader = ResourceLoader.GetForViewIndependentUse();
            ViewModel.Selected = e.ClickedItem as Entry;
            EntryDialog.Title = resourceLoader.GetString("UpdateEntryTitle");
            EntryDialog.PrimaryButtonText = resourceLoader.GetString("Update");
            EntryDialog.PrimaryButtonCommand = UpdateCommand;
            var clone = ViewModel.Selected.Clone();
            EntryDialog.DataContext = clone;
            await EntryDialog.ShowAsync();
        }

        private void Add()
        {
            ViewModel.Add(EntryDialog.DataContext as Entry);
        }

        private void Update()
        {
            ViewModel.Update(Entries.SelectedIndex, EntryDialog.DataContext as Entry);
        }

        private void Delete()
        {
            ViewModel.DeleteSelected();
        }

        private void UpdateAdditionalLines()
        {
            ViewModel.UpdateAdditionalLines(AdditionalLines.Text);
        }

        private void Grid_RightTapped(object sender, RightTappedRoutedEventArgs e)
        {
            var owner = sender as FrameworkElement;
            if (owner != null)
            {
                var flyoutBase = FlyoutBase.GetAttachedFlyout(owner);
                flyoutBase.ShowAt(owner, new FlyoutShowOptions
                {
                    Position = e.GetPosition(owner),
                });
            }
        }

        private async void Delete_Click(object sender, RoutedEventArgs e)
        {
            var menuFlyoutItem = sender as MenuFlyoutItem;

            if (menuFlyoutItem != null)
            {
                var selectedEntry = menuFlyoutItem.DataContext as Entry;
                ViewModel.Selected = selectedEntry;
                DeleteDialog.Title = selectedEntry.Address;
                await DeleteDialog.ShowAsync();
            }
        }

        private void Enable_Click(object sender, RoutedEventArgs e)
        {
            var menuFlyoutItem = sender as MenuFlyoutItem;

            if (menuFlyoutItem != null)
            {
                ViewModel.Selected = menuFlyoutItem.DataContext as Entry;
                ViewModel.EnableSelected();
            }
        }

        private void Disable_Click(object sender, RoutedEventArgs e)
        {
            var menuFlyoutItem = sender as MenuFlyoutItem;

            if (menuFlyoutItem != null)
            {
                ViewModel.Selected = menuFlyoutItem.DataContext as Entry;
                ViewModel.DisableSelected();
            }
        }

        private async void Ping_Click(object sender, RoutedEventArgs e)
        {
            var menuFlyoutItem = sender as MenuFlyoutItem;

            if (menuFlyoutItem != null)
            {
                ViewModel.Selected = menuFlyoutItem.DataContext as Entry;
                await ViewModel.PingSelectedAsync();
            }
        }

        private async void Page_Loaded(object sender, RoutedEventArgs e)
        {
            var userSettings = App.GetService<IUserSettings>();
            if (userSettings.ShowStartupWarning)
            {
                var resourceLoader = ResourceLoader.GetForViewIndependentUse();
                var dialog = new ContentDialog();

                dialog.XamlRoot = XamlRoot;
                dialog.Style = Application.Current.Resources["DefaultContentDialogStyle"] as Style;
                dialog.Title = resourceLoader.GetString("Warning");
                dialog.Content = new TextBlock
                {
                    Text = resourceLoader.GetString("WarningDialog_Text"),
                    TextWrapping = TextWrapping.Wrap,
                };
                dialog.PrimaryButtonText = resourceLoader.GetString("WarningDialog_Accept");
                dialog.CloseButtonText = resourceLoader.GetString("WarningDialog_Quit");
                dialog.CloseButtonCommand = ExitCommand;

                await dialog.ShowAsync();
            }
        }
    }
}
