#include "RiptideMainMenu.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "RiptideCrewFigure.h"
#include "RiptideGameInstance.h"
#include "RiptideMenuGameMode.h"
#include "RiptideMenuWidgets.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RiptideMainMenu"

using namespace RiptideMenuStyle;

namespace
{
	/** The left column's width on each screen, and the menu's margins. */
	constexpr float LeftMargin = 110.f;
	constexpr float HomeWidth = 440.f;
	constexpr float PanelWidth = 640.f;
	constexpr float JoinWidth = 980.f;

	/** The crew screen's preview: the booth's render target, turned by dragging (or Q/E, LB/RB). */
	class SRiptideCrewView : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRiptideCrewView) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
			SLATE_ARGUMENT(TWeakObjectPtr<ARiptideCrewPreview>, Preview)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Brush = InArgs._Brush;
			Preview = InArgs._Preview;
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(576.f, 720.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Size = FVector2f(G.GetLocalSize());
			Box(Out, LayerId, G, FVector2f(0.f), Size, PanelBrush(), Srgb(0.02f, 0.03f, 0.04f, 0.92f));
			if (Brush && Brush->GetResourceObject())
			{
				FSlateDrawElement::MakeBox(Out, LayerId + 1, G.ToPaintGeometry(Size - FVector2f(8.f), FSlateLayoutTransform(FVector2f(4.f))), Brush);
			}
			else
			{
				DrawString(Out, LayerId + 1, G, LOCTEXT("NoPreview", "No preview").ToString(), Font(EFont::Regular, 14), Size * 0.5f, Dim(), 0.5f);
			}
			const FString Hint = LOCTEXT("TurnHint", "Drag to turn  ·  Q / E  ·  LB / RB").ToString();
			DrawString(Out, LayerId + 2, G, Hint, Font(EFont::Regular, 13), FVector2f(Size.X * 0.5f, Size.Y - 34.f), Dim(), 0.5f);
			return LayerId + 3;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			bDragging = true;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			bDragging = false;
			return FReply::Handled().ReleaseMouseCapture();
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (bDragging && Preview.IsValid())
			{
				Preview->Turn(MouseEvent.GetCursorDelta().X * 0.6f);
			}
			return FReply::Handled();
		}

	private:
		const FSlateBrush* Brush = nullptr;
		TWeakObjectPtr<ARiptideCrewPreview> Preview;
		bool bDragging = false;
	};

	TSharedRef<STextBlock> Label(const FText& String, const FSlateFontInfo& FontInfo, const FLinearColor& Colour, float WrapAt = 0.f)
	{
		return SNew(STextBlock).Text(String).Font(FontInfo).ColorAndOpacity(Colour).WrapTextAt(WrapAt);
	}
}

void SRiptideMainMenu::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	MenuCamera = InArgs._MenuCamera;
	Preview = InArgs._Preview;
	if (Game.IsValid())
	{
		EditingLook = Game->GetAppearance();
	}
	if (Preview.IsValid() && Preview->GetRenderTarget())
	{
		PreviewBrush.SetResourceObject(Preview->GetRenderTarget());
		PreviewBrush.ImageSize = FVector2D(ARiptideCrewPreview::Width, ARiptideCrewPreview::Height);
		PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SRiptideBackdrop)
		]
		// The title, and the screen under it, down the left.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(LeftMargin, 70.f, 0.f, 90.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeTitle()
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(Switcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()[ MakeHome() ]
				+ SWidgetSwitcher::Slot()[ MakeHost() ]
				+ SWidgetSwitcher::Slot()[ MakeJoin() ]
				+ SWidgetSwitcher::Slot()[ MakeCrew() ]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SBox).WidthOverride(PanelWidth)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							ScreenHeader(LOCTEXT("SettingsTitle", "Settings"), LOCTEXT("SettingsBlurb", "Changes take effect at once and are saved when you go back."))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(Settings, SRiptideSettingsPanel)
							.WorldContext(Game)
							.OnBack_Lambda([this]() { ShowScreen(ERiptideMenuScreen::Home); })
						]
					]
				]
			]
		]
		// The crew member, turning on the right while the crew screen is open.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 40.f, 150.f, 40.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Current == ERiptideMenuScreen::Crew ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SRiptideCrewView).Brush(&PreviewBrush).Preview(Preview)
			]
		]
		// The banner.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(LeftMargin, 0.f, 0.f, 84.f)
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() { return FSlateApplication::Get().GetCurrentTime() < MessageUntil ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.BorderImage(CardHotBrush())
			.BorderBackgroundColor(Srgb(0.12f, 0.08f, 0.05f, 0.95f))
			.Padding(FMargin(20.f, 14.f))
			[
				SNew(STextBlock).Text_Lambda([this]() { return Message; }).Font(Font(EFont::Regular, 17)).ColorAndOpacity(Ink()).WrapTextAt(760.f)
			]
		]
		+ SOverlay::Slot().VAlign(VAlign_Bottom)
		[
			MakeFooter()
		]
		+ SOverlay::Slot()
		[
			MakeBusy()
		]
	];

	if (Game.IsValid())
	{
		const FText Returned = Game->TakeMenuMessage();
		if (!Returned.IsEmpty())
		{
			ShowMessage(Returned);
		}
	}
}

TSharedRef<SWidget> SRiptideMainMenu::MakeTitle()
{
	// Big on the home screen; on the others it shrinks to a header so the screen has the room.
	auto IsHome = [this]() { return Current == ERiptideMenuScreen::Home; };
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Title", "RIPTIDE"))
			.Font_Lambda([IsHome]() { return IsHome() ? Font(EFont::Black, 112, 220) : Font(EFont::Black, 40, 260); })
			.ColorAndOpacity(Ink())
			.ShadowOffset(FVector2D(0.f, 3.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 2.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(56.f).HeightOverride(3.f)
				[
					SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Accent())
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("Tagline", "CO-OP NAVAL SURVIVAL")).Font(Font(EFont::Condensed, 16, 420)).ColorAndOpacity(Heading())
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride_Lambda([IsHome]() { return IsHome() ? 70.f : 34.f; })
		];
}

TSharedRef<SWidget> SRiptideMainMenu::ScreenHeader(const FText& Title, const FText& Blurb)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(FText::FromString(Title.ToString().ToUpper()), Font(EFont::Condensed, 34, 120), Ink())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 18.f)
		[
			Label(Blurb, Font(EFont::Regular, 15), Dim(), JoinWidth)
		];
}

TSharedRef<SWidget> SRiptideMainMenu::MakeHome()
{
	auto Item = [this](const FText& Name, const FText& Detail, TFunction<void()> Action) -> TSharedRef<SRiptideButton>
	{
		return SNew(SRiptideButton).Text(Name).Detail(Detail).Width(HomeWidth).Height(70.f).FontSize(24)
			.OnClicked_Lambda([Action]() { Action(); });
	};
	TSharedRef<SRiptideButton> Host = Item(LOCTEXT("Host", "Host game"), LOCTEXT("HostDetail", "Take the boat out. Your crew joins you."),
		[this]() { ShowScreen(ERiptideMenuScreen::Host); });
	FirstControl.Add(ERiptideMenuScreen::Home, Host);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ Host ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Item(LOCTEXT("Join", "Join game"), LOCTEXT("JoinDetail", "Find a crew, or join a friend by IP."), [this]() { ShowScreen(ERiptideMenuScreen::Join); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Item(LOCTEXT("Crew", "Crew"), LOCTEXT("CrewDetail", "Your callsign, and how you look and kit out."), [this]() { OpenCrew(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Item(LOCTEXT("Settings", "Settings"), LOCTEXT("SettingsDetail", "Controls, sound and graphics."), [this]() { ShowScreen(ERiptideMenuScreen::Settings); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Item(LOCTEXT("Quit", "Quit"), FText::GetEmpty(), [this]() { if (Game.IsValid()) { Game->QuitToDesktop(); } })
		];
}

TSharedRef<SWidget> SRiptideMainMenu::MakeHost()
{
	auto Steam = [this]() { return Game.IsValid() && Game->IsUsingSteam(); };
	TSharedRef<SRiptideButton> Friends = SNew(SRiptideButton)
		.Text(LOCTEXT("FriendsOnly", "Friends only"))
		.Detail(LOCTEXT("FriendsOnlyDetail", "Your Steam friends can join, and anyone you invite."))
		.Width(PanelWidth).Height(74.f)
		.Selected_Lambda([this, Steam]() { return bFriendsOnly && Steam(); })
		.OnClicked_Lambda([this]() { bFriendsOnly = true; });
	Friends->SetEnabled(TAttribute<bool>::CreateLambda(Steam));
	TSharedRef<SRiptideButton> Public = SNew(SRiptideButton)
		.Text(LOCTEXT("Public", "Public"))
		.Detail(LOCTEXT("PublicDetail", "Listed in every player's game browser."))
		.Width(PanelWidth).Height(74.f)
		.Selected_Lambda([this, Steam]() { return !bFriendsOnly || !Steam(); })
		.OnClicked_Lambda([this]() { bFriendsOnly = false; });
	TSharedRef<SRiptideButton> HostButton = SNew(SRiptideButton)
		.Text(LOCTEXT("HostNow", "Host"))
		.Width(300.f).Height(58.f).Centred(true).Primary(true)
		.OnClicked_Lambda([this]()
		{
			if (Game.IsValid() && !Game->HostGame(bFriendsOnly))
			{
				ShowMessage(LOCTEXT("Busy", "Hold on: something else is still under way."));
			}
		});
	FirstControl.Add(ERiptideMenuScreen::Host, HostButton);
	return SNew(SBox).WidthOverride(PanelWidth)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			ScreenHeader(LOCTEXT("HostTitle", "Host game"), LOCTEXT("HostBlurb", "You start on the patrol boat; up to three more crew can join you."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[ Friends ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[ Public ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 22.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this, Steam]()
			{
				if (Steam())
				{
					return LOCTEXT("SteamHostNote", "Once you're in, open the menu (Esc) to invite friends through Steam.");
				}
				const FString Address = Game.IsValid() ? Game->GetLocalAddress() : FString();
				return FText::Format(LOCTEXT("LanHostNote", "Steam isn't running, so the game is open to your local network, and to anyone with your IP "
					"address ({0} on this network). Players outside it need port 7777 (UDP) forwarded to this PC."),
					FText::FromString(Address.IsEmpty() ? TEXT("unknown") : Address));
			})
			.Font(Font(EFont::Regular, 15)).ColorAndOpacity(Dim()).WrapTextAt(PanelWidth)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)[ HostButton ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SRiptideButton).Text(LOCTEXT("Back", "Back")).Width(200.f).Height(58.f).Centred(true)
				.OnClicked_Lambda([this]() { ShowScreen(ERiptideMenuScreen::Home); })
			]
		]
	];
}

TSharedRef<SWidget> SRiptideMainMenu::MakeJoin()
{
	TSharedRef<SRiptideButton> Refresh = SNew(SRiptideButton)
		.Text(LOCTEXT("Refresh", "Refresh")).Width(200.f).Height(54.f).FontSize(20).Centred(true)
		.OnClicked_Lambda([this]() { if (Game.IsValid()) { SelectedGame = INDEX_NONE; Game->FindGames(); } });
	Refresh->SetEnabled(TAttribute<bool>::CreateLambda([this]() { return Game.IsValid() && Game->GetActivity() == ERiptideOnlineActivity::None; }));
	TSharedRef<SRiptideButton> Join = SNew(SRiptideButton)
		.Text(LOCTEXT("JoinSelected", "Join")).Width(240.f).Height(54.f).FontSize(20).Centred(true).Primary(true)
		.OnClicked_Lambda([this]() { JoinSelected(); });
	Join->SetEnabled(TAttribute<bool>::CreateLambda([this]()
	{
		return Game.IsValid() && Game->GetFoundGames().IsValidIndex(SelectedGame) && Game->GetActivity() == ERiptideOnlineActivity::None;
	}));
	FirstControl.Add(ERiptideMenuScreen::Join, Refresh);
	AddressField = MakeTextField(LOCTEXT("AddressHint", "Host's IP address, e.g. 192.168.1.20"), 18, FOnTextChanged(),
		FOnTextCommitted::CreateLambda([this](const FText&, ETextCommit::Type How)
		{
			if (How == ETextCommit::OnEnter)
			{
				JoinTypedAddress();
			}
		}));

	auto Column = [](const FText& Name) { return Label(Name, Font(EFont::Condensed, 14, 200), Heading()); };
	return SNew(SBox).WidthOverride(JoinWidth)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			ScreenHeader(LOCTEXT("JoinTitle", "Join game"), LOCTEXT("JoinBlurb", "Games you can join right now. Select one and press Join, or double-click it."))
		]
		// Column headings, lined up with the rows' columns.
		+ SVerticalBox::Slot().AutoHeight().Padding(22.f, 0.f, 0.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)[ Column(LOCTEXT("ColHost", "HOST")) ]
			+ SHorizontalBox::Slot().FillWidth(0.17f)[ Column(LOCTEXT("ColCrew", "CREW")) ]
			+ SHorizontalBox::Slot().FillWidth(0.13f)[ Column(LOCTEXT("ColPing", "PING")) ]
			+ SHorizontalBox::Slot().FillWidth(0.2f)[ Column(LOCTEXT("ColGame", "")) ]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(PanelBrush()).BorderBackgroundColor(Srgb(0.02f, 0.035f, 0.045f, 0.85f)).Padding(6.f)
			[
				SNew(SBox).HeightOverride(330.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(GameList, SVerticalBox)
						]
					]
					// Searching, or why the list is empty.
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return Game.IsValid() && Game->GetFoundGames().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
						[
							SNew(SRiptideSpinner)
							.Visibility_Lambda([this]() { return Game.IsValid() && Game->GetActivity() == ERiptideOnlineActivity::Searching ? EVisibility::Visible : EVisibility::Collapsed; })
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								if (!Game.IsValid())
								{
									return FText::GetEmpty();
								}
								return Game->GetActivity() == ERiptideOnlineActivity::Searching ? Game->GetStatusText() : Game->GetSearchSummary();
							})
							.Font(Font(EFont::Regular, 16)).ColorAndOpacity(Dim()).WrapTextAt(JoinWidth - 120.f).Justification(ETextJustify::Center)
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)[ Join ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)[ Refresh ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SRiptideButton).Text(LOCTEXT("Back", "Back")).Width(200.f).Height(54.f).FontSize(20).Centred(true)
				.OnClicked_Lambda([this]() { ShowScreen(ERiptideMenuScreen::Home); })
			]
		]
		// Join by IP: for playing without Steam, or when the browser can't see the game.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 26.f, 0.f, 8.f)
		[
			Label(LOCTEXT("ByIp", "JOIN BY IP ADDRESS"), Font(EFont::Condensed, 15, 220), Heading())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SBox).HeightOverride(54.f)[ AddressField.ToSharedRef() ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SRiptideButton).Text(LOCTEXT("Connect", "Connect")).Width(220.f).Height(54.f).FontSize(20).Centred(true)
				.OnClicked_Lambda([this]() { JoinTypedAddress(); })
			]
		]
	];
}

void SRiptideMainMenu::RebuildGameList()
{
	if (!GameList.IsValid() || !Game.IsValid())
	{
		return;
	}
	ShownListVersion = Game->GetFoundGamesVersion();
	GameList->ClearChildren();
	const TArray<FRiptideGameListing>& Found = Game->GetFoundGames();
	if (!Found.IsValidIndex(SelectedGame))
	{
		SelectedGame = Found.IsEmpty() ? INDEX_NONE : 0;
	}
	for (int32 i = 0; i < Found.Num(); ++i)
	{
		const FRiptideGameListing& Listing = Found[i];
		const FText Ping = Listing.PingMs >= 0 ? FText::FromString(FString::Printf(TEXT("%d ms"), Listing.PingMs)) : FText::FromString(TEXT("—"));
		const FText Badge = Listing.bFriend ? LOCTEXT("FriendTag", "Friend") : Listing.bFriendsOnly ? LOCTEXT("FriendsOnlyTag", "Friends only")
			: FText::GetEmpty();
		TArray<FText> Columns = { FText::FromString(FString::Printf(TEXT("%d / %d"), Listing.Players, Listing.MaxPlayers)), Ping, Badge };
		GameList->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SRiptideButton)
			.Text(FText::FromString(Listing.HostName))
			.Columns(Columns)
			.ColumnPositions({ 0.5f, 0.67f, 0.8f })
			.Width(JoinWidth - 24.f).Height(50.f).FontSize(20)
			.Selected_Lambda([this, i]() { return SelectedGame == i; })
			.OnClicked_Lambda([this, i]() { SelectedGame = i; })
			.OnDoubleClicked_Lambda([this, i]() { SelectedGame = i; JoinSelected(); })
		];
	}
}

void SRiptideMainMenu::JoinSelected()
{
	if (Game.IsValid() && Game->GetFoundGames().IsValidIndex(SelectedGame) && !Game->JoinGame(SelectedGame))
	{
		ShowMessage(LOCTEXT("CantJoin", "Couldn't start joining that game. Refresh and try again."));
	}
}

void SRiptideMainMenu::JoinTypedAddress()
{
	if (!Game.IsValid() || !AddressField.IsValid())
	{
		return;
	}
	if (!Game->JoinByAddress(AddressField->GetText().ToString()))
	{
		const FText Why = Game->TakeMenuMessage();
		ShowMessage(Why.IsEmpty() ? LOCTEXT("CantConnect", "Couldn't connect to that address.") : Why);
	}
}

TSharedRef<SWidget> SRiptideMainMenu::MakeCrew()
{
	// Only what a callsign may hold, as it's typed.
	CallsignField = MakeTextField(LOCTEXT("CallsignHint", "Your callsign"), 20, FOnTextChanged::CreateLambda([this](const FText& Typed)
	{
		const FString Clean = URiptideGameInstance::SanitiseCallsign(Typed.ToString());
		if (CallsignField.IsValid() && Clean != Typed.ToString() && !Typed.ToString().EndsWith(TEXT(" ")))
		{
			CallsignField->SetText(FText::FromString(Clean));
		}
	}));
	CallsignField->SetText(FText::FromString(Game.IsValid() ? Game->GetCallsign() : FString()));

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 i = 0; i < int32(ERiptideLook::Count); ++i)
	{
		const ERiptideLook Part = ERiptideLook(i);
		Rows->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Width(PanelWidth).Height(38.f)
			.Label(URiptideAppearanceLibrary::GetPartName(Part))
			.Value_Lambda([this, Part]() { return URiptideAppearanceLibrary::GetOptionName(Part, EditingLook.Get(Part)); })
			.OnStep_Lambda([this, Part](int32 Dir)
			{
				const int32 Count = URiptideAppearanceLibrary::GetOptionCount(Part);
				EditingLook.Set(Part, uint8(((EditingLook.Get(Part) + Dir) % Count + Count) % Count));
				if (Preview.IsValid())
				{
					Preview->SetLook(EditingLook);
				}
			})
		];
	}

	TSharedRef<SRiptideButton> Save = SNew(SRiptideButton).Text(LOCTEXT("SaveCrew", "Save")).Width(200.f).Height(54.f).FontSize(20).Centred(true).Primary(true)
		.OnClicked_Lambda([this]() { SaveCrew(); });
	FirstControl.Add(ERiptideMenuScreen::Crew, CallsignField);
	return SNew(SBox).WidthOverride(PanelWidth)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(LOCTEXT("CrewTitle", "CREW"), Font(EFont::Condensed, 34, 120), Ink())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 6.f)
		[
			Label(LOCTEXT("CallsignLabel", "CALLSIGN"), Font(EFont::Condensed, 15, 220), Heading())
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SBox).HeightOverride(50.f)[ CallsignField.ToSharedRef() ]
		]
		+ SVerticalBox::Slot().AutoHeight()[ Rows ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)[ Save ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SRiptideButton).Text(LOCTEXT("Randomise", "Randomise")).Width(210.f).Height(54.f).FontSize(20).Centred(true)
				.OnClicked_Lambda([this]()
				{
					EditingLook = URiptideAppearanceLibrary::RandomAppearance();
					if (Preview.IsValid())
					{
						Preview->SetLook(EditingLook);
					}
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SRiptideButton).Text(LOCTEXT("Back", "Back")).Width(200.f).Height(54.f).FontSize(20).Centred(true)
				.OnClicked_Lambda([this]() { ShowScreen(ERiptideMenuScreen::Home); })
			]
		]
	];
}

void SRiptideMainMenu::OpenCrew()
{
	ShowScreen(ERiptideMenuScreen::Crew);
}

void SRiptideMainMenu::SaveCrew()
{
	if (!Game.IsValid())
	{
		return;
	}
	const FString Callsign = URiptideGameInstance::SanitiseCallsign(CallsignField.IsValid() ? CallsignField->GetText().ToString() : FString());
	if (Callsign.IsEmpty())
	{
		ShowMessage(LOCTEXT("NeedCallsign", "Your callsign can't be empty: letters, digits, - _ and . only."));
		return;
	}
	Game->SetProfile(Callsign, EditingLook, true);
	if (MenuCamera.IsValid())
	{
		MenuCamera->SetCrewLook(EditingLook);
	}
	ShowMessage(FText::Format(LOCTEXT("Saved", "Saved. You'll join games as {0}."), FText::FromString(Callsign)));
	ShowScreen(ERiptideMenuScreen::Home);
}

TSharedRef<SWidget> SRiptideMainMenu::MakeFooter()
{
	return SNew(SBox).Padding(FMargin(LeftMargin, 0.f, 60.f, 34.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(LOCTEXT("CallsignFooter", "CALLSIGN")).Font(Font(EFont::Condensed, 14, 220)).ColorAndOpacity(Heading())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 26.f, 0.f)
		[
			SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Game.IsValid() ? Game->GetCallsign() : FString()); })
			.Font(Font(EFont::Bold, 16)).ColorAndOpacity(Ink())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text_Lambda([this]() { return Game.IsValid() ? Game->GetOnlineServiceText() : FText::GetEmpty(); })
			.Font(Font(EFont::Regular, 14)).ColorAndOpacity(Dim())
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Controls", "Arrows or stick: move   ·   Enter or A: choose   ·   Left / Right: change   ·   Esc or B: back"))
			.Font(Font(EFont::Regular, 14)).ColorAndOpacity(Dim())
		]
	];
}

TSharedRef<SWidget> SRiptideMainMenu::MakeBusy()
{
	TSharedRef<SRiptideButton> Cancel = SNew(SRiptideButton).Text(LOCTEXT("Cancel", "Cancel")).Width(200.f).Height(50.f).FontSize(18).Centred(true)
		.OnClicked_Lambda([this]() { if (Game.IsValid()) { Game->CancelJoin(); } });
	Cancel->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return Game.IsValid() && Game->GetActivity() == ERiptideOnlineActivity::Joining ? EVisibility::Visible : EVisibility::Collapsed;
	}));
	BusyCancel = Cancel;
	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return IsBusy() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SOverlay::Slot()
		[
			SNew(SRiptideBackdrop).Full(true)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder).BorderImage(PanelBrush()).BorderBackgroundColor(Srgb(0.02f, 0.035f, 0.045f, 0.95f)).Padding(FMargin(40.f, 30.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)[ SNew(SRiptideSpinner) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text_Lambda([this]() { return Game.IsValid() ? Game->GetStatusText() : FText::GetEmpty(); })
						.Font(Font(EFont::Regular, 20)).ColorAndOpacity(Ink())
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 22.f, 0.f, 0.f)[ Cancel ]
			]
		];
}

bool SRiptideMainMenu::IsBusy() const
{
	const ERiptideOnlineActivity Activity = Game.IsValid() ? Game->GetActivity() : ERiptideOnlineActivity::None;
	return Activity == ERiptideOnlineActivity::Hosting || Activity == ERiptideOnlineActivity::Joining || Activity == ERiptideOnlineActivity::Leaving;
}

void SRiptideMainMenu::ShowScreen(ERiptideMenuScreen Screen)
{
	const ERiptideMenuScreen Previous = Current;
	if (Screen == ERiptideMenuScreen::Crew && Previous != ERiptideMenuScreen::Crew && Game.IsValid())
	{
		// Start from what's saved; nothing changes until Save.
		EditingLook = Game->GetAppearance();
		if (CallsignField.IsValid())
		{
			CallsignField->SetText(FText::FromString(Game->GetCallsign()));
		}
		if (Preview.IsValid())
		{
			Preview->SetLook(EditingLook);
		}
	}
	Current = Screen;
	if (Switcher.IsValid())
	{
		Switcher->SetActiveWidgetIndex(int32(Screen));
	}
	if (Preview.IsValid())
	{
		Preview->SetFilming(Screen == ERiptideMenuScreen::Crew);
	}
	if (Screen == ERiptideMenuScreen::Join && Previous != ERiptideMenuScreen::Join && Game.IsValid())
	{
		SelectedGame = INDEX_NONE;
		Game->FindGames();
	}
	if (Screen == ERiptideMenuScreen::Settings && Settings.IsValid())
	{
		Settings->FocusFirst();
		return;
	}
	FocusScreen();
}

void SRiptideMainMenu::FocusScreen()
{
	if (const TSharedPtr<SWidget>* First = FirstControl.Find(Current))
	{
		PendingFocus = *First;
		Focus(*First);
	}
	else if (Current == ERiptideMenuScreen::Settings && Settings.IsValid())
	{
		Settings->FocusFirst();
	}
}

void SRiptideMainMenu::ShowMessage(const FText& InMessage)
{
	Message = InMessage;
	MessageUntil = FSlateApplication::Get().GetCurrentTime() + 10.0;
}

void SRiptideMainMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!Game.IsValid())
	{
		return;
	}
	if (PendingFocus.IsValid())
	{
		Focus(PendingFocus);
		if (PendingFocus->HasAnyUserFocus().IsSet())
		{
			PendingFocus.Reset();
		}
	}
	if (!PreviewBrush.GetResourceObject() && Preview.IsValid() && Preview->GetRenderTarget())
	{
		// The booth makes its render target when it starts, which can be after the menu is built.
		PreviewBrush.SetResourceObject(Preview->GetRenderTarget());
		PreviewBrush.ImageSize = FVector2D(ARiptideCrewPreview::Width, ARiptideCrewPreview::Height);
		PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	if (Game->GetFoundGamesVersion() != ShownListVersion)
	{
		RebuildGameList();
	}
	const FText Returned = Game->TakeMenuMessage();
	if (!Returned.IsEmpty())
	{
		ShowMessage(Returned);
	}
	// The status card takes focus while it's up (its Cancel), and gives it back to the screen after.
	const bool bBusy = IsBusy();
	if (bBusy != bWasBusy)
	{
		bWasBusy = bBusy;
		if (bBusy)
		{
			// Its Cancel when it has one (joining); otherwise the menu itself, which swallows the keys meanwhile.
			PendingFocus = BusyCancel.IsValid() && BusyCancel->GetVisibility().IsVisible() ? BusyCancel : TSharedPtr<SWidget>(SharedThis(this));
			Focus(PendingFocus);
		}
		else
		{
			FocusScreen();
		}
	}
}

FReply SRiptideMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsBusy())
	{
		if (IsBackKey(InKeyEvent) && Game.IsValid())
		{
			Game->CancelJoin();
		}
		return FReply::Handled();
	}
	if (IsBackKey(InKeyEvent) && Current != ERiptideMenuScreen::Home)
	{
		ShowScreen(ERiptideMenuScreen::Home);
		return FReply::Handled();
	}
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(0);
	const bool bTyping = Focused.IsValid() && Focused->GetType() == FName(TEXT("SEditableText"));
	if (Current == ERiptideMenuScreen::Crew && Preview.IsValid() && !bTyping)
	{
		const FKey Key = InKeyEvent.GetKey();
		if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)
		{
			Preview->Turn(-30.f);
			return FReply::Handled();
		}
		if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder)
		{
			Preview->Turn(30.f);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SRiptideMainMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A click on empty space stays with the menu (it mustn't take focus away to the game behind).
	return FReply::Handled();
}

FNavigationReply SRiptideMainMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// Nothing focused on the screen (the menu itself has it): the first press just finds the first control.
	if (HasKeyboardFocus())
	{
		FocusScreen();
		return FNavigationReply::Stop();
	}
	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

// --- The HUD ---

void ARiptideMenuHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* Player = GetOwningPlayerController();
	if (!Player || !Player->IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	ARiptideMenuGameMode* Mode = GetWorld()->GetAuthGameMode<ARiptideMenuGameMode>();
	Menu = SNew(SRiptideMainMenu)
		.Game(GetGameInstance<URiptideGameInstance>())
		.MenuCamera(Mode ? Mode->GetMenuCamera() : nullptr)
		.Preview(Mode ? Mode->GetCrewPreview() : nullptr);
	GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), 5);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Menu);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(InputMode);
	Player->SetShowMouseCursor(true);
	Menu->FocusScreen();
}

void ARiptideMenuHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Menu.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	}
	Menu.Reset();
	Super::EndPlay(EndPlayReason);
}

void ARiptideMenuHUD::ShowScreen(ERiptideMenuScreen Screen)
{
	if (Menu.IsValid())
	{
		Menu->ShowScreen(Screen);
	}
}

#undef LOCTEXT_NAMESPACE
