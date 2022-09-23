﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Globalization;
using System.Linq;
using System.Unicode;
using System.Windows;
using System.Windows.Media.TextFormatting;
using PowerAccent.Core.Services;
using PowerAccent.Core.Tools;
using PowerToys.PowerAccentKeyboardService;

namespace PowerAccent.Core;

public class PowerAccent : IDisposable
{
    private readonly SettingsService _settingService;

    // Keys that show a description (like dashes) when ShowCharacterInfoSetting is 1
    private readonly LetterKey[] _letterKeysShowingDescription = new LetterKey[] { LetterKey.VK_O };

    private bool _visible;
    private char[] _characters = Array.Empty<char>();
    private UnicodeCharInfo[] _characterNames = Array.Empty<UnicodeCharInfo>();
    private int _selectedIndex = -1;
    private bool _showDescription;

    public LetterKey[] LetterKeysShowingDescription => _letterKeysShowingDescription;

    public bool ShowDescription => _showDescription;

    public UnicodeCharInfo[] CharacterNames => _characterNames;

    public event Action<bool, char[]> OnChangeDisplay;

    public event Action<int, char> OnSelectCharacter;

    private readonly KeyboardListener _keyboardListener;

    public PowerAccent()
    {
        _keyboardListener = new KeyboardListener();
        _keyboardListener.InitHook();
        _settingService = new SettingsService(_keyboardListener);

        SetEvents();
    }

    private void SetEvents()
    {
        _keyboardListener.SetShowToolbarEvent(new PowerToys.PowerAccentKeyboardService.ShowToolbar((LetterKey letterKey) =>
        {
            Application.Current.Dispatcher.Invoke(() =>
            {
                ShowToolbar(letterKey);
            });
        }));

        _keyboardListener.SetHideToolbarEvent(new PowerToys.PowerAccentKeyboardService.HideToolbar((InputType inputType) =>
        {
            Application.Current.Dispatcher.Invoke(() =>
            {
                SendInputAndHideToolbar(inputType);
            });
        }));

        _keyboardListener.SetNextCharEvent(new PowerToys.PowerAccentKeyboardService.NextChar((TriggerKey triggerKey) =>
        {
            Application.Current.Dispatcher.Invoke(() =>
            {
                ProcessNextChar(triggerKey);
            });
        }));
    }

    private void ShowToolbar(LetterKey letterKey)
    {
        _visible = true;
        _characters = WindowsFunctions.IsCapitalState() ? ToUpper(SettingsService.GetDefaultLetterKey(letterKey)) : SettingsService.GetDefaultLetterKey(letterKey);
        _characterNames = GetCharacterNames(_characters);

        Microsoft.PowerToys.Settings.UI.Library.Enumerations.PowerAccentShowDescription characterInfoSetting = _settingService.ShowDescription;
        _showDescription = characterInfoSetting == Microsoft.PowerToys.Settings.UI.Library.Enumerations.PowerAccentShowDescription.Always || (characterInfoSetting == Microsoft.PowerToys.Settings.UI.Library.Enumerations.PowerAccentShowDescription.SpecialCharacters && ((IList<LetterKey>)_letterKeysShowingDescription).Contains(letterKey));

        Task.Delay(_settingService.InputTime).ContinueWith(
        t =>
        {
            if (_visible)
            {
                OnChangeDisplay?.Invoke(true, _characters);
            }
        }, TaskScheduler.FromCurrentSynchronizationContext());
    }

    private UnicodeCharInfo[] GetCharacterNames(char[] characters)
    {
        UnicodeCharInfo[] charInfoCollection = Array.Empty<UnicodeCharInfo>();
        foreach (char character in characters)
        {
            charInfoCollection = charInfoCollection.Append<UnicodeCharInfo>(UnicodeInfo.GetCharInfo(character)).ToArray<UnicodeCharInfo>();
        }

        return charInfoCollection;
    }

    private void SendInputAndHideToolbar(InputType inputType)
    {
        switch (inputType)
        {
            case InputType.Space:
                {
                    WindowsFunctions.Insert(' ');
                    break;
                }

            case InputType.Char:
                {
                    if (_selectedIndex != -1)
                    {
                        WindowsFunctions.Insert(_characters[_selectedIndex], true);
                    }

                    break;
                }
        }

        OnChangeDisplay?.Invoke(false, null);
        _selectedIndex = -1;
        _visible = false;
    }

    private void ProcessNextChar(TriggerKey triggerKey)
    {
        if (_visible && _selectedIndex == -1)
        {
            if (triggerKey == TriggerKey.Left)
            {
                _selectedIndex = (_characters.Length / 2) - 1;
            }

            if (triggerKey == TriggerKey.Right)
            {
                _selectedIndex = _characters.Length / 2;
            }

            if (triggerKey == TriggerKey.Space)
            {
                _selectedIndex = 0;
            }

            if (_selectedIndex < 0)
            {
                _selectedIndex = 0;
            }

            if (_selectedIndex > _characters.Length - 1)
            {
                _selectedIndex = _characters.Length - 1;
            }

            OnSelectCharacter?.Invoke(_selectedIndex, _characters[_selectedIndex]);
            return;
        }

        if (triggerKey == TriggerKey.Space)
        {
            if (_selectedIndex < _characters.Length - 1)
            {
                ++_selectedIndex;
            }
            else
            {
                _selectedIndex = 0;
            }
        }

        if (triggerKey == TriggerKey.Left && _selectedIndex > 0)
        {
            --_selectedIndex;
        }

        if (triggerKey == TriggerKey.Right && _selectedIndex < _characters.Length - 1)
        {
            ++_selectedIndex;
        }

        OnSelectCharacter?.Invoke(_selectedIndex, _characters[_selectedIndex]);
    }

    public Point GetDisplayCoordinates(Size window)
    {
        var activeDisplay = WindowsFunctions.GetActiveDisplay();
        Rect screen = new Rect(activeDisplay.Location, activeDisplay.Size) / activeDisplay.Dpi;
        Position position = _settingService.Position;

        /* Debug.WriteLine("Dpi: " + activeDisplay.Dpi); */

        return Calculation.GetRawCoordinatesFromPosition(position, screen, window);
    }

    public void Dispose()
    {
        _keyboardListener.UnInitHook();
        GC.SuppressFinalize(this);
    }

    public static char[] ToUpper(char[] array)
    {
        char[] result = new char[array.Length];
        for (int i = 0; i < array.Length; i++)
        {
            result[i] = char.ToUpper(array[i], System.Globalization.CultureInfo.InvariantCulture);
        }

        return result;
    }
}
